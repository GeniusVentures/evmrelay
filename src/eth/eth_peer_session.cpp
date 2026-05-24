// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_handshake_guard.hpp>
#include <eth/eth_peer_session.hpp>
#include <base/parse_utility.hpp>
#include <base/rlp-logger.hpp>
#include <rlpx/protocol/messages.hpp>
#include <algorithm>
#include <string>

namespace {

void log_status_fields(
    const char*              direction,
    const eth::StatusMessage& status,
    uint8_t                  negotiated_eth_version,
    uint8_t                  negotiated_eth_offset) noexcept
{
    static auto log = rlp::base::createLogger("eth.status");
    const auto common = eth::get_common_fields(status);
    log->info("{} ETH Status negotiated_version={} offset=0x{:02x} status_version={} network_id={} genesis={} fork_hash={} fork_next={}",
              direction,
              static_cast<int>(negotiated_eth_version),
              negotiated_eth_offset,
              static_cast<int>(common.protocol_version),
              common.network_id,
              rlp::base::parse::hex_array_string(common.genesis_hash),
              rlp::base::parse::hex_array_string(common.fork_id.fork_hash),
              common.fork_id.next_fork);
}

const char* status_validation_error_string(eth::StatusValidationError error) noexcept
{
    switch (error)
    {
    case eth::StatusValidationError::kProtocolVersionMismatch:
        return "protocol_version_mismatch";
    case eth::StatusValidationError::kNetworkIDMismatch:
        return "network_id_mismatch";
    case eth::StatusValidationError::kGenesisMismatch:
        return "genesis_mismatch";
    case eth::StatusValidationError::kInvalidBlockRange:
        return "invalid_block_range";
    }
    return "unknown";
}

std::string bytes_hex(const rlpx::ByteBuffer& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    const size_t limit = std::min<size_t>(bytes.size(), 160U);
    std::string out;
    out.reserve(limit * 2U);
    for (size_t i = 0; i < limit; ++i)
    {
        const auto byte = static_cast<uint8_t>(bytes[i]);
        out.push_back(kHex[byte >> 4U]);
        out.push_back(kHex[byte & 0x0fU]);
    }
    return out;
}

} // namespace

namespace eth {

StatusMessage BuildLocalStatusMessage(
    uint8_t        negotiated_protocol_version,
    uint64_t       network_id,
    const Hash256& genesis_hash,
    const ForkId&  fork_id) noexcept
{
    if (negotiated_protocol_version <= kEthProtocolVersion68)
    {
        StatusMessage68 status68;
        status68.protocol_version = negotiated_protocol_version;
        status68.network_id = network_id;
        status68.genesis_hash = genesis_hash;
        status68.fork_id = fork_id;
        status68.td = 0;
        status68.blockhash = genesis_hash;
        return status68;
    }

    StatusMessage69 status69;
    status69.protocol_version = negotiated_protocol_version;
    status69.network_id = network_id;
    status69.genesis_hash = genesis_hash;
    status69.fork_id = fork_id;
    status69.earliest_block = 0;
    status69.latest_block = 0;
    status69.latest_block_hash = genesis_hash;
    return status69;
}

protocol::ValidationResult ValidateRemoteStatusMessage(
    const StatusMessage& remote_status,
    uint8_t              negotiated_protocol_version,
    uint64_t             expected_network_id,
    const Hash256&       expected_genesis_hash) noexcept
{
    const auto common = get_common_fields(remote_status);
    if (common.protocol_version != negotiated_protocol_version)
    {
        return StatusValidationError::kProtocolVersionMismatch;
    }

    return protocol::validate_status(remote_status, expected_network_id, expected_genesis_hash);
}

rlp::outcome::result<EthStatusHandshakeResult, StatusValidationError, rlp::outcome::policy::all_narrow>
PerformEthStatusHandshake(
    const EthStatusHandshakeStart& start,
    boost::asio::yield_context     yield) noexcept
{
    if (!start.channel)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    const uint8_t negotiated_eth_version = start.channel->negotiated_eth_version();
    const uint8_t negotiated_eth_offset = start.channel->negotiated_eth_offset();
    if (negotiated_eth_version == 0U || negotiated_eth_offset == 0U)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    const auto status = BuildLocalStatusMessage(
        negotiated_eth_version,
        start.network_id,
        start.genesis_hash,
        start.fork_id);
    log_status_fields(
        "sending",
        status,
        negotiated_eth_version,
        negotiated_eth_offset);

    auto encoded = protocol::encode_status(status);
    if (!encoded)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    rlpx::framing::Message status_message{};
    status_message.id = static_cast<uint8_t>(negotiated_eth_offset + protocol::kStatusMessageId);
    status_message.payload = std::move(encoded.value());
    const auto post_result = start.channel->post_message(std::move(status_message));
    if (!post_result)
    {
        return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
    }

    bool status_received = false;
    for (;;)
    {
        auto inbound_result = start.channel->receive_message_with_timeout(
            protocol::kStatusHandshakeTimeout,
            yield);
        if (!inbound_result)
        {
            static auto log = rlp::base::createLogger("eth.status");
            log->warn("ETH Status handshake failed after sending local Status: remote closed or timed out before accepted Status");
            return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
        }

        rlpx::protocol::Message inbound_message{};
        inbound_message.id = inbound_result.value().id;
        inbound_message.payload = std::move(inbound_result.value().payload);

        static auto log = rlp::base::createLogger("eth.status");
        if (inbound_message.id == rlpx::kDisconnectMessageId)
        {
            const auto disconnect = rlpx::protocol::DisconnectMessage::decode(
                rlpx::ByteView(inbound_message.payload.data(), inbound_message.payload.size()));
            if (disconnect)
            {
                log->warn("Remote sent RLPx Disconnect during ETH Status handshake after local Status, reason={}",
                             static_cast<int>(disconnect.value().reason));
                if (start.remote_disconnect_handler)
                {
                    start.remote_disconnect_handler(disconnect.value().reason);
                }
            }
            else
            {
                log->warn("Remote sent undecodable RLPx Disconnect during ETH Status handshake after local Status");
            }
            return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
        }

        const auto eth_id = NormalizeEthWireMessageId(inbound_message.id, negotiated_eth_offset);
        if (!eth_id.has_value())
        {
            log->debug("Ignoring non-ETH handshake message id=0x{:02x} while waiting for remote Status",
                       inbound_message.id);
            continue;
        }

        if (*eth_id != protocol::kStatusMessageId)
        {
            if (!status_received)
            {
                log->warn("Remote sent ETH message id=0x{:02x} before Status during ETH Status handshake",
                             *eth_id);
                return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
            }
            continue;
        }

        const rlp::ByteView payload(inbound_message.payload.data(), inbound_message.payload.size());
        const auto decoded_status = start.eth_message_schemas.empty()
            ? protocol::decode_status(payload)
            : protocol::decode_status(payload, start.eth_message_schemas);
        if (!decoded_status)
        {
            log->warn("Remote ETH Status decode failed during handshake, payload_size={} error={} payload_hex=0x{}",
                      inbound_message.payload.size(),
                      static_cast<int>(decoded_status.error()),
                      bytes_hex(inbound_message.payload));
            return rlp::outcome::failure(StatusValidationError::kProtocolVersionMismatch);
        }

        log_status_fields(
            "received remote",
            decoded_status.value(),
            negotiated_eth_version,
            negotiated_eth_offset);

        const auto validation = ValidateRemoteStatusMessage(
            decoded_status.value(),
            negotiated_eth_version,
            start.network_id,
            start.genesis_hash);
        if (!validation)
        {
            log->warn("Rejected remote ETH Status locally: validation_error={}",
                         status_validation_error_string(validation.error()));
            return rlp::outcome::failure(validation.error());
        }

        status_received = true;
        log_status_fields(
            "accepted remote",
            decoded_status.value(),
            negotiated_eth_version,
            negotiated_eth_offset);

        EthStatusHandshakeResult result{};
        result.remote_status = decoded_status.value();
        return result;
    }
}

bool StartEthStatusHandshake(
    const EthStatusHandshakeStart& start) noexcept
{
    if (!start.channel)
    {
        return false;
    }

    start.channel->set_eth_message_handler(start.inbound_message_handler);
    return true;
}

} // namespace eth
