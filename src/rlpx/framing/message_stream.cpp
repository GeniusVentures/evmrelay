// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/framing/message_stream.hpp>
#include <rlp/rlp_encoder.hpp>
#include <rlp/rlp_decoder.hpp>
#include <base/rlp-logger.hpp>
#include <snappy.h>
#include <cstring>

namespace rlpx::framing {

namespace asio = boost::asio;

MessageStream::MessageStream(
    std::unique_ptr<FrameCipher> cipher,
    socket::SocketTransport transport
) noexcept
    : cipher_(std::move(cipher))
    , transport_(std::move(transport)) {
    // Pre-allocate buffers to reduce allocations
    send_buffer_.reserve(4096);
    recv_buffer_.reserve(4096);
}

void MessageStream::close() noexcept
{
    (void)transport_.close();
}

VoidResult MessageStream::send_message(const MessageSendParams& params, asio::yield_context yield) noexcept {
    // go-ethereum wire format: RLP-encoded uint(message_id) || payload
    // When Snappy is enabled, ONLY the payload is compressed; the message ID stays uncompressed.
    // Mirrors go-ethereum p2p/rlpx/rlpx.go: Write() = rlp.AppendUint64(code) + snappy.Encode(data)

    // Encode message ID as RLP varint (uncompressed in all cases).
    rlp::RlpEncoder id_encoder;
    if (auto res = id_encoder.add(params.message_id); !res) {
        return SessionError::kInvalidMessage;
    }
    auto id_result = id_encoder.GetBytes();
    if (!id_result) return SessionError::kInvalidMessage;
    ByteBuffer id_bytes = detail::from_rlp_bytes(*id_result.value());

    // Build payload part (optionally Snappy-compressed).
    ByteBuffer payload_bytes;
    if (!params.payload.empty()) {
        if (compression_enabled_) {
            std::string compressed;
            snappy::Compress(
                reinterpret_cast<const char*>(params.payload.data()),
                params.payload.size(),
                &compressed
            );
            payload_bytes.assign(compressed.begin(), compressed.end());
        } else {
            payload_bytes.assign(params.payload.begin(), params.payload.end());
        }
    }

    // Combine: [RLP(msg_id)] || [payload (maybe compressed)]
    ByteBuffer message_data;
    message_data.reserve(id_bytes.size() + payload_bytes.size());
    message_data.insert(message_data.end(), id_bytes.begin(), id_bytes.end());
    message_data.insert(message_data.end(), payload_bytes.begin(), payload_bytes.end());
        // Frame the message (may need to split into multiple frames)
        // For now, send as single frame (max 16MB)
        if ( message_data.size() > kMaxFrameSize ) {
            return SessionError::kInvalidMessage;
        }
        
        FrameEncryptParams frame_params{};
        frame_params.frame_data = message_data;
        frame_params.is_first_frame = true;
        
        auto encrypted_frame_result = cipher_->encrypt_frame(frame_params);
        if ( !encrypted_frame_result ) {
            return SessionError::kEncryptionError;
        }
        
        // Send encrypted frame over socket
        auto write_result = transport_.write_all(encrypted_frame_result.value(), yield);
        if ( !write_result ) {
            return write_result.error();
        }
        
        return outcome::success();
}

Result<Message> MessageStream::receive_message(asio::yield_context yield) noexcept {
    static auto log = rlp::base::createLogger("rlpx.frame");

    // Receive encrypted frame from socket
    auto frame_result = receive_frame(yield);
    if ( !frame_result || frame_result.value().empty() ) {
        SPDLOG_LOGGER_INFO(log, "receive_message: receive_frame failed or returned empty payload");
        return SessionError::kInvalidMessage;
    }
        
        const auto& frame_data = frame_result.value();
        SPDLOG_LOGGER_INFO(log, "receive_message: frame decrypted payload_size={}", frame_data.size());

        // go-ethereum wire format: RLP-encoded uint(code) || payload (maybe Snappy-compressed)
        // Mirrors go-ethereum p2p/rlpx/rlpx.go: Read() = rlp.SplitUint64 + snappy.Decode(rest)
        rlp::RlpDecoder id_decoder(detail::to_rlp_view(frame_data));

        uint64_t msg_id = 0;
        if (!id_decoder.read(msg_id)) {
            SPDLOG_LOGGER_INFO(log, "receive_message: failed to decode message id from frame payload_size={}",
                               frame_data.size());
            return SessionError::kInvalidMessage;
        }

        // Remaining bytes are the payload (Snappy-compressed if enabled, raw otherwise).
        ByteView remaining_view = id_decoder.Remaining();
        ByteBuffer payload;

        if (compression_enabled_ && !remaining_view.empty()) {
            size_t uncompressed_len = 0;
            if (!snappy::GetUncompressedLength(
                    reinterpret_cast<const char*>(remaining_view.data()),
                    remaining_view.size(),
                    &uncompressed_len)) {
                SPDLOG_LOGGER_INFO(log, "receive_message: failed to read Snappy length for msg_id=0x{:02x}",
                                   static_cast<uint8_t>(msg_id));
                return SessionError::kInvalidMessage;
            }
            payload.resize(uncompressed_len);
            if (!snappy::RawUncompress(
                    reinterpret_cast<const char*>(remaining_view.data()),
                    remaining_view.size(),
                    reinterpret_cast<char*>(payload.data()))) {
                SPDLOG_LOGGER_INFO(log, "receive_message: Snappy decode failed for msg_id=0x{:02x}",
                                   static_cast<uint8_t>(msg_id));
                return SessionError::kInvalidMessage;
            }
        } else if (!remaining_view.empty()) {
            payload.assign(remaining_view.begin(), remaining_view.end());
        }

        SPDLOG_LOGGER_INFO(log, "receive_message: decoded msg_id=0x{:02x} payload_size={} compression_enabled={}",
                           static_cast<uint8_t>(msg_id),
                           payload.size(),
                           compression_enabled_);
        Message msg{static_cast<uint8_t>(msg_id), std::move(payload)};
        return msg;
}

FramingResult<void> MessageStream::send_frame(ByteView frame_data, asio::yield_context yield) noexcept {
    // Encrypt and send frame
    FrameEncryptParams params{};
    params.frame_data = frame_data;
    params.is_first_frame = true;
        
        auto encrypted_result = cipher_->encrypt_frame(params);
        if ( !encrypted_result ) {
            return encrypted_result.error();
        }
        
        // Send over socket
        auto write_result = transport_.write_all(encrypted_result.value(), yield);
        if ( !write_result ) {
            return FramingError::kEncryptionFailed;
        }
        
        return outcome::success();
}

FramingResult<ByteBuffer> MessageStream::receive_frame(asio::yield_context yield) noexcept {
    auto header_with_mac_result = transport_.read_exact(kFrameHeaderWithMacSize, yield);
    if ( !header_with_mac_result ) {
        ByteBuffer empty;
        return empty;
    }
        
        const auto& header_data = header_with_mac_result.value();
        
        // Split into header and MAC
        gsl::span<const uint8_t, kFrameHeaderSize> header_span(
            header_data.data(), 
            kFrameHeaderSize
        );
        gsl::span<const uint8_t, kMacSize> header_mac_span(
            header_data.data() + kFrameHeaderSize, 
            kMacSize
        );
        
        // Decrypt header to get frame size
        auto frame_size_result = cipher_->decrypt_header(header_span, header_mac_span);
        if ( !frame_size_result ) {
            ByteBuffer empty;
            return empty;
        }
        
        size_t frame_size = frame_size_result.value();
        
        // Frame body is padded to 16-byte boundary on the wire; followed by 16-byte MAC.
        const size_t padding    = (frame_size % kFramePaddingAlignment != 0)
                                ? (kFramePaddingAlignment - (frame_size % kFramePaddingAlignment))
                                : 0;
        const size_t padded_size = frame_size + padding;
        size_t total_frame_bytes = padded_size + kMacSize;
        auto frame_with_mac_result = transport_.read_exact(total_frame_bytes, yield);
        if ( !frame_with_mac_result ) {
            ByteBuffer empty;
            return empty;
        }
        
        const auto& frame_data = frame_with_mac_result.value();
        
        // Split into frame and MAC
        ByteView frame_ciphertext(frame_data.data(), padded_size);
        gsl::span<const uint8_t, kMacSize> frame_mac_span(
            frame_data.data() + padded_size, 
            kMacSize
        );
        
        // Decrypt frame body — header MAC already advanced in decrypt_header above
        auto decrypted_result = cipher_->decrypt_frame_body(
            frame_size,
            frame_ciphertext,
            ByteView(frame_mac_span.data(), kMacSize)
        );
        if ( !decrypted_result ) {
            ByteBuffer empty;
            return empty;
        }
        
        return decrypted_result.value();
}

} // namespace rlpx::framing
