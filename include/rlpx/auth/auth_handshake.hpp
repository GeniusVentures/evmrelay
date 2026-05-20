// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_RLPX_AUTH_AUTH_HANDSHAKE_HPP
#define EVMRELAY_INCLUDE_RLPX_AUTH_AUTH_HANDSHAKE_HPP

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"
#include "auth_keys.hpp"
#include "../socket/socket_transport.hpp"
#include <boost/asio/spawn.hpp>

namespace rlpx::auth {

/// Plain RLPx v4 fixed wire sizes (ECIES overhead + plaintext)
/// auth plaintext  = sig(65) + eph_hash(32) + pubkey(64) + nonce(32) + ver(1) = 194
/// ECIES overhead  = pubkey(65) + iv(16) + mac(32) = 113
static constexpr size_t kAuthSize = 307; ///< ECIES( 194-byte auth body )
/// ack plaintext   = eph_pubkey(64) + nonce(32) + ver(1) = 97
static constexpr size_t kAckSize  = 210; ///< ECIES( 97-byte ack body )

// Handshake configuration parameters
struct HandshakeConfig {
    PublicKey        local_public_key{};
    PrivateKey       local_private_key{};
    std::string      client_id;
    uint16_t         listen_port = 0;
    // Optional for initiator (outbound), empty for recipient (inbound)
    std::optional<PublicKey> peer_public_key;
};

// Handshake result containing all derived material
struct HandshakeResult {
    AuthKeyMaterial key_material;
    FrameSecrets frame_secrets;
    std::string peer_client_id;
    uint16_t peer_listen_port;
    std::optional<socket::SocketTransport> transport; ///< Socket to hand off to MessageStream after handshake

    // Grouped access to crypto material
    [[nodiscard]] const AuthKeyMaterial& keys() const noexcept { 
        return key_material; 
    }
    
    [[nodiscard]] const FrameSecrets& secrets() const noexcept { 
        return frame_secrets; 
    }
};

/// Authentication handshake coordinator.
class AuthHandshake {
public:
    /// @brief Construct handshake with config and an already-connected transport.
    /// @param config    Crypto config (keys, peer pubkey, client id).
    /// @param transport Connected TCP socket — ownership transferred in.
    explicit AuthHandshake(const HandshakeConfig& config,
                           socket::SocketTransport transport) noexcept;

    /// @brief Execute full handshake (auth + hello exchange).
    /// @param yield Boost.Asio stackful coroutine context.
    /// @return HandshakeResult on success, SessionError on failure.
    [[nodiscard]] Result<HandshakeResult>
    execute(boost::asio::yield_context yield) noexcept;

    /// State query.
    [[nodiscard]] bool is_initiator() const noexcept { 
        return config_.peer_public_key.has_value(); 
    }

    /// @brief Derive RLPx frame secrets from authenticated handshake key material.
    /// @param keys         All ECDH, nonce, and wire bytes collected during the handshake.
    /// @param is_initiator True for the connection initiator, false for the responder.
    /// @return FrameSecrets containing AES/MAC keys and MAC seed byte strings.
    [[nodiscard]] static FrameSecrets
    derive_frame_secrets(const AuthKeyMaterial& keys, bool is_initiator) noexcept;

private:
    /// @brief Internal auth phase (sends/receives auth messages).
    /// @param yield Boost.Asio stackful coroutine context.
    [[nodiscard]] AuthResult<AuthKeyMaterial>
    perform_auth(boost::asio::yield_context yield) noexcept;

    /// @brief Internal hello exchange (capability negotiation).
    /// @param aes_key AES key derived from handshake.
    /// @param mac_key MAC key derived from handshake.
    /// @param yield   Boost.Asio stackful coroutine context.
    [[nodiscard]] Result<void>
    exchange_hello(ByteView aes_key, ByteView mac_key, boost::asio::yield_context yield) noexcept;


    HandshakeConfig config_;
    socket::SocketTransport transport_;
    // Socket abstraction injected via constructor
};

/// @brief Derive RLPx frame secrets from authenticated handshake key material.
/// @details Exposed as a free function for unit testing against go-ethereum test vectors.
/// @param keys         All ECDH, nonce, and wire bytes collected during the handshake.
/// @param is_initiator True for the connection initiator (dialer), false for the responder.
/// @return FrameSecrets containing AES/MAC keys and MAC seed byte strings.
[[nodiscard]] FrameSecrets
derive_frame_secrets(const AuthKeyMaterial& keys, bool is_initiator) noexcept;

} // namespace rlpx::auth

#endif // EVMRELAY_INCLUDE_RLPX_AUTH_AUTH_HANDSHAKE_HPP
