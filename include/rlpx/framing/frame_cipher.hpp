// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_RLPX_FRAMING_FRAME_CIPHER_HPP
#define EVMRELAY_INCLUDE_RLPX_FRAMING_FRAME_CIPHER_HPP

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"
#include "../auth/auth_keys.hpp"
#include <memory>

namespace rlpx::framing {

// Frame encryption parameters
struct FrameEncryptParams {
    ByteView frame_data;
    bool is_first_frame;
};

// Frame decryption parameters
struct FrameDecryptParams {
    ByteView header_ciphertext;
    ByteView header_mac;
    ByteView frame_ciphertext;
    ByteView frame_mac;
};

// Frame cipher for RLPx message encryption
class FrameCipher {
public:
    // Initialize with secrets from handshake
    explicit FrameCipher(const auth::FrameSecrets& secrets) noexcept;

    ~FrameCipher();

    // Encrypt frame: returns [header || header_mac || frame || frame_mac]
    [[nodiscard]] FramingResult<ByteBuffer>
    encrypt_frame(const FrameEncryptParams& params) noexcept;

    // Decrypt header to get frame size
    [[nodiscard]] FramingResult<size_t>
    decrypt_header(
        gsl::span<const uint8_t, kFrameHeaderSize> header_ciphertext,
        gsl::span<const uint8_t, kMacSize> header_mac
    ) noexcept;

    /// @brief Decrypt a complete frame (header + body) in one call.
    /// @param params  Bundled header ciphertext, header MAC, frame ciphertext, and frame MAC.
    /// @return Decrypted plaintext payload, or a FramingError on MAC mismatch / invalid size.
    [[nodiscard]] FramingResult<ByteBuffer>
    decrypt_frame(const FrameDecryptParams& params) noexcept;

    /// @brief Decrypt the frame body only, after @ref decrypt_header has already been called.
    ///
    /// Use this when the header has been decrypted separately (e.g. in @ref MessageStream)
    /// to avoid advancing the MAC accumulator twice.
    /// @param fsize           Plaintext byte count as decoded from the header.
    /// @param frame_ct_padded Frame ciphertext padded to a 16-byte boundary.
    /// @param frame_mac       16-byte frame MAC from the wire.
    /// @return Decrypted plaintext (truncated to @p fsize), or a FramingError on MAC mismatch.
    [[nodiscard]] FramingResult<ByteBuffer>
    decrypt_frame_body(size_t fsize,
                       ByteView frame_ct_padded,
                       ByteView frame_mac) noexcept;

    // Return const reference to secrets (grouped values)
    [[nodiscard]] const auth::FrameSecrets& secrets() const noexcept {
        return secrets_;
    }

private:
    // Update rolling MAC state — legacy stubs, logic now in FrameCipherImpl
    void update_egress_mac(ByteView data) noexcept;
    void update_ingress_mac(ByteView data) noexcept;

    [[nodiscard]] MacDigest compute_header_mac(ByteView header_ct) noexcept;
    [[nodiscard]] MacDigest compute_frame_mac(ByteView frame_ct) noexcept;

    auth::FrameSecrets secrets_;

    struct FrameCipherImpl;
    std::unique_ptr<FrameCipherImpl> impl_;  ///< Pimpl for OpenSSL CTR and hashMAC state
};

} // namespace rlpx::framing

#endif // EVMRELAY_INCLUDE_RLPX_FRAMING_FRAME_CIPHER_HPP
