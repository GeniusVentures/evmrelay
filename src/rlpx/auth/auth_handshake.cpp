// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/auth/auth_handshake.hpp>
#include <rlpx/auth/ecies_cipher.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <rlpx/crypto/kdf.hpp>
#include <rlpx/crypto/hmac.hpp>
#include <base/rlp-logger.hpp>
#include <rlp/rlp_encoder.hpp>
#include <rlp/rlp_decoder.hpp>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>
#include <nil/crypto3/hash/accumulators/hash.hpp>
#include <cstring>

namespace rlpx::auth {

namespace asio = boost::asio;

namespace {
    rlp::base::Logger& auth_log() {
        static auto log = rlp::base::createLogger("rlpx.auth");
        return log;
    }


    std::string pubkey_hex(gsl::span<const uint8_t, kPublicKeySize> pubkey)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(kPublicKeySize * 2U);
        for (uint8_t b : pubkey)
        {
            out.push_back(kHex[(b >> 4U) & 0x0FU]);
            out.push_back(kHex[b & 0x0FU]);
        }
        return out;
    }

// Create auth message (initiator -> responder)
// Format: 2-byte-len-prefix || ECIES(RLP(authMsgV4) || random_padding)
// Matches go-ethereum sealEIP8 / makeAuthMsg exactly.
AuthResult<ByteBuffer> create_auth_message(
    gsl::span<const uint8_t, kPrivateKeySize> local_private_key,
    gsl::span<const uint8_t, kPublicKeySize>  local_public_key,
    const PublicKey&                          ephemeral_public_key,
    const PrivateKey&                         ephemeral_private_key,
    const Nonce&                              nonce,
    gsl::span<const uint8_t, kPublicKeySize>  remote_public_key
) noexcept {
    // ── 1. static shared secret = ECDH(local_priv, remote_pub) ──
    // go-ethereum: staticSharedSecret = GenerateShared(remote, sskLen=16, macLen=16)
    // GenerateShared returns x.Bytes() zero-padded to skLen+macLen=32 bytes (raw x-coordinate).
    auto* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx) { return AuthError::kSignatureInvalid; }

    auto static_ss_result = rlpx::crypto::Ecdh::compute_shared_secret(remote_public_key, local_private_key);
    if (!static_ss_result) {
        auth_log()->debug("create_auth_message: compute_shared_secret failed (code {})",
                          static_cast<int>(static_ss_result.error()));
        secp256k1_context_destroy(ctx);
        return AuthError::kSharedSecretFailed;
    }
    // token = raw 32-byte x-coordinate (compute_shared_secret already returns x-coord)
    const auto& token = static_ss_result.value();

    // ── 2. signed = xor(token, initNonce) ──
    std::array<uint8_t, kNonceSize> signed_msg;
    for (size_t i = 0; i < kNonceSize; ++i) {
        signed_msg[i] = token[i] ^ nonce[i];
    }

    // ── 3. signature = Sign(signed_msg, ephemeral_priv) — recoverable ──
    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, signed_msg.data(),
                                          ephemeral_private_key.data(), nullptr, nullptr)) {
        auth_log()->debug("create_auth_message: sign_recoverable failed");
        secp256k1_context_destroy(ctx);
        return AuthError::kSignatureInvalid;
    }
    std::array<uint8_t, kEcdsaCompactSigSize> sig_compact;
    int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, sig_compact.data(), &recid, &sig);
    secp256k1_context_destroy(ctx);

    // Full 65-byte signature: compact(64) || recid(1)
    std::array<uint8_t, kEcdsaSigSize> signature;
    std::memcpy(signature.data(), sig_compact.data(), kEcdsaCompactSigSize);
    signature[kEcdsaCompactSigSize] = static_cast<uint8_t>(recid);

    // ── 4. RLP-encode authMsgV4 ──
    // go-ethereum struct: [Signature[65], InitiatorPubkey[64], Nonce[32], Version uint(=4)]
    // RLP list of: bytes(65), bytes(64), bytes(32), uint(4)
    rlp::RlpEncoder enc;
    if (!enc.BeginList()) { return AuthError::kSignatureInvalid; }
    if (!enc.add(rlp::ByteView(signature.data(), signature.size()))) { return AuthError::kSignatureInvalid; }
    if (!enc.add(rlp::ByteView(local_public_key.data(), local_public_key.size()))) { return AuthError::kSignatureInvalid; }
    if (!enc.add(rlp::ByteView(nonce.data(), nonce.size()))) { return AuthError::kSignatureInvalid; }
    if (!enc.add(static_cast<uint64_t>(kAuthVersion))) { return AuthError::kSignatureInvalid; }
    if (!enc.EndList()) { return AuthError::kSignatureInvalid; }

    auto rlp_result = enc.MoveBytes();
    if (!rlp_result) { return AuthError::kSignatureInvalid; }
    ByteBuffer rlp_body(rlp_result.value().begin(), rlp_result.value().end());

    // ── 5. Append fixed random padding (current implementation keeps 100 bytes) ──
    {
        ByteBuffer padding(kEip8AuthPaddingSize);
        RAND_bytes(padding.data(), static_cast<int>(padding.size()));
        rlp_body.insert(rlp_body.end(), padding.begin(), padding.end());
    }

    // ── 6. EIP-8 prefix = uint16_be(len(rlp_body) + eciesOverhead) ──
    const auto prefix_val = static_cast<uint16_t>(rlp_body.size() + kEciesOverheadSize);
    ByteBuffer prefix = { static_cast<uint8_t>(prefix_val >> 8U),
                          static_cast<uint8_t>(prefix_val & 0xFFU) };

    // ── 7. ECIES encrypt with prefix as shared_mac_data ──
    EciesEncryptParams params{
        ByteView(rlp_body.data(), rlp_body.size()),
        remote_public_key,
        ByteView(prefix.data(), prefix.size())
    };

    auth_log()->debug("create_auth_message: RLP+padding body={} bytes, prefix=0x{:02x}{:02x}",
                      rlp_body.size(), prefix[0], prefix[1]);
    auto ecies_result = EciesCipher::encrypt(params);
    if (!ecies_result) {
        auth_log()->debug("create_auth_message: EciesCipher::encrypt failed (code {})",
                          static_cast<int>(ecies_result.error()));
    } else {
        auth_log()->debug("create_auth_message: ECIES encrypt ok, ciphertext={} bytes", ecies_result.value().size());
    }
    return ecies_result;
}

// Parse auth message (responder)
AuthResult<AuthKeyMaterial> parse_auth_message(
    ByteView encrypted_auth,
    gsl::span<const uint8_t, kPrivateKeySize> local_private_key,
    ByteView shared_mac_data
) noexcept {
    // Decrypt with ECIES
    EciesDecryptParams params{
        encrypted_auth,
        local_private_key,
        shared_mac_data
    };

    auto auth_body_result = EciesCipher::decrypt(params);
    if (!auth_body_result) {
        return auth_body_result.error();
    }
    auto auth_body = std::move(auth_body_result.value());

    // Parse auth body: signature(kEcdsaSigSize) || eph-pubk-hash(kNonceSize) || pubk(kPublicKeySize) || nonce(kNonceSize) || version(kAuthVersionSize)
    if ( auth_body.size() < kEcdsaSigSize + kNonceSize + kPublicKeySize + kNonceSize + kAuthVersionSize ) {
        return AuthError::kInvalidAuthMessage;
    }

    AuthKeyMaterial keys;

    // Extract signature
    size_t offset = 0;
    std::array<uint8_t, kEcdsaSigSize> signature;
    std::memcpy(signature.data(), auth_body.data() + offset, kEcdsaSigSize);
    offset += kEcdsaSigSize;

    // Extract ephemeral public key hash
    std::array<uint8_t, kNonceSize> eph_pub_hash;
    std::memcpy(eph_pub_hash.data(), auth_body.data() + offset, kNonceSize);
    offset += kNonceSize;

    // Extract initiator public key
    std::memcpy(keys.peer_public_key.data(), auth_body.data() + offset, kPublicKeySize);
    offset += kPublicKeySize;

    // Extract nonce
    std::memcpy(keys.initiator_nonce.data(), auth_body.data() + offset, kNonceSize);
    offset += kNonceSize;

    // Version byte (currently unused)
    // uint8_t version = auth_body[offset];

    // Store the auth message for MAC computation later
    keys.initiator_auth_message = std::move(auth_body);

    return keys;
}

// Create ack message (responder -> initiator)
// Format: ECIES(responder-ephemeral-pubk || responder-nonce || 0x00)
AuthResult<ByteBuffer> create_ack_message(
    const PublicKey& ephemeral_public_key,
    const Nonce& nonce,
    gsl::span<const uint8_t, kPublicKeySize> remote_public_key
) noexcept {
    ByteBuffer ack_body;
    // ephemeral pubkey(kPublicKeySize) + nonce(kNonceSize) + version(kAuthVersionSize)
    ack_body.reserve(kPublicKeySize + kNonceSize + kAuthVersionSize);

    // Add ephemeral public key
    ack_body.insert(ack_body.end(), ephemeral_public_key.begin(), ephemeral_public_key.end());

    // Add nonce
    ack_body.insert(ack_body.end(), nonce.begin(), nonce.end());

    // Add version byte
    ack_body.push_back(kAuthVersion);

    // Encrypt with ECIES
    EciesEncryptParams params{
        ByteView(ack_body.data(), ack_body.size()),
        remote_public_key,
        ByteView{}
    };

    return EciesCipher::encrypt(params);
}

// Parse ack message (initiator) — plaintext is RLP-encoded authRespV4 + padding.
// go-ethereum authRespV4: [RandomPubkey[64], Nonce[32], Version uint]
AuthResult<void> parse_ack_message(
    ByteView encrypted_ack,
    gsl::span<const uint8_t, kPrivateKeySize> local_private_key,
    ByteView shared_mac_data,
    AuthKeyMaterial& keys
) noexcept {
    // Decrypt with ECIES — shared_mac_data is the 2-byte EIP-8 length prefix
    EciesDecryptParams params{
        encrypted_ack,
        local_private_key,
        shared_mac_data
    };

    auto ack_body_result = EciesCipher::decrypt(params);
    if (!ack_body_result)
    {
        return ack_body_result.error();
    }
    const auto& ack_plain = ack_body_result.value();

    // RLP-decode: list header, then RandomPubkey(64 bytes), Nonce(32 bytes), Version
    rlp::ByteView view(ack_plain.data(), ack_plain.size());
    rlp::RlpDecoder dec(view);

    auto list_result = dec.ReadListHeaderBytes();
    if (!list_result) { return AuthError::kInvalidAckMessage; }

    // RandomPubkey — 64 bytes
    rlp::Bytes pubkey_bytes;
    if (!dec.read(pubkey_bytes)) { return AuthError::kInvalidAckMessage; }
    if (pubkey_bytes.size() != kPublicKeySize) { return AuthError::kInvalidAckMessage; }
    std::memcpy(keys.peer_ephemeral_public_key.data(), pubkey_bytes.data(), kPublicKeySize);

    // Nonce — 32 bytes
    rlp::Bytes nonce_bytes;
    if (!dec.read(nonce_bytes)) { return AuthError::kInvalidAckMessage; }
    if (nonce_bytes.size() != kNonceSize) { return AuthError::kInvalidAckMessage; }
    std::memcpy(keys.recipient_nonce.data(), nonce_bytes.data(), kNonceSize);

    // Version and padding are intentionally ignored (forward-compat per EIP-8)

    return outcome::success();
}

} // anonymous namespace

AuthHandshake::AuthHandshake(const HandshakeConfig& config,
                             socket::SocketTransport transport) noexcept
    : config_(config)
    , transport_(std::move(transport)) {
}

// Note: Uses Boost.Asio stackful coroutines (yield_context) for socket I/O — C++17 compatible.
Result<HandshakeResult> AuthHandshake::execute(asio::yield_context yield) noexcept {
    const std::string remote_addr = transport_.remote_address();
    const uint16_t remote_port = transport_.remote_port();
    const std::string remote_pubkey_hex = config_.peer_public_key.has_value()
        ? pubkey_hex(config_.peer_public_key.value())
        : std::string{};

    // Generate ephemeral keypair
    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if ( !keypair_result ) {
        auth_log()->debug("execute: generate_ephemeral_keypair failed");
        return SessionError::kAuthenticationFailed;
    }
    auto keypair = keypair_result.value();

    // Generate nonce
    Nonce local_nonce;
    if ( RAND_bytes(local_nonce.data(), kNonceSize) != 1 ) {
        auth_log()->debug("execute: RAND_bytes failed");
        return SessionError::kAuthenticationFailed;
    }

    HandshakeResult result;
    result.key_material.local_ephemeral_public_key  = keypair.public_key;
    result.key_material.local_ephemeral_private_key = keypair.private_key;

    if ( is_initiator() ) {
        // ── Initiator: send auth ────────────────────────────────────────────
        result.key_material.initiator_nonce = local_nonce;

        auth_log()->debug("execute: calling create_auth_message");
        auto auth_msg_result = create_auth_message(
            config_.local_private_key,
            config_.local_public_key,
            keypair.public_key,
            keypair.private_key,
            local_nonce,
            config_.peer_public_key.value()
        );
        if ( !auth_msg_result ) {
            auth_log()->debug("execute: create_auth_message failed (code {})",
                              static_cast<int>(auth_msg_result.error()));
            return SessionError::kAuthenticationFailed;
        }
        auth_log()->debug("execute: auth message built ({} bytes), sending", auth_msg_result.value().size());

        // Build full wire auth = prefix(2) || ciphertext
        // Store full wire bytes for MAC derivation (go-ethereum uses authPacket in secrets())
        const auto& auth_ciphertext = auth_msg_result.value();
        const auto  prefix_val      = static_cast<uint16_t>(auth_ciphertext.size());
        ByteBuffer  auth_wire;
        auth_wire.reserve(kEip8LengthPrefixSize + auth_ciphertext.size());
        auth_wire.push_back(static_cast<uint8_t>(prefix_val >> 8U));
        auth_wire.push_back(static_cast<uint8_t>(prefix_val & 0xFFU));
        auth_wire.insert(auth_wire.end(), auth_ciphertext.begin(), auth_ciphertext.end());

        // Store full wire bytes for MAC derivation
        result.key_material.initiator_auth_message = auth_wire;

        auto send_result = transport_.write_all(
            ByteView(auth_wire.data(), auth_wire.size()), yield);
        if ( !send_result ) {
            auth_log()->debug("execute: write_all(auth) failed");
            return SessionError::kAuthenticationFailed;
        }
        auth_log()->debug("execute: auth sent ({} bytes wire), waiting for ack length prefix", auth_wire.size());

        // ── Initiator: receive ack ──────────────────────────────────────────
        // EIP-8 ack wire: 2-byte len(ack_ciphertext) || ack_ciphertext
        auto len_result = transport_.read_exact(kEip8LengthPrefixSize, yield);
        if ( !len_result ) {
            auth_log()->debug("execute: peer {}:{} pubkey={} read_exact(ack length prefix) failed",
                              remote_addr,
                              remote_port,
                              remote_pubkey_hex);
            return SessionError::kAuthenticationFailed;
        }
        const auto& len_bytes   = len_result.value();
        const size_t ack_body_len = (static_cast<size_t>(len_bytes[0]) << 8U)
                                  |  static_cast<size_t>(len_bytes[1]);
        if (ack_body_len > kMaxEip8HandshakePacketSize) {
            auth_log()->debug("execute: peer {}:{} pubkey={} ack length {} exceeds EIP-8 max {}",
                              remote_addr,
                              remote_port,
                              remote_pubkey_hex,
                              ack_body_len,
                              kMaxEip8HandshakePacketSize);
            return SessionError::kAuthenticationFailed;
        }
        auth_log()->debug("execute: ack length prefix received, ack_body_len={}", ack_body_len);

        auto ack_result = transport_.read_exact(ack_body_len, yield);
        if ( !ack_result ) {
            auth_log()->debug("execute: read_exact(ack body) failed");
            return SessionError::kAuthenticationFailed;
        }
        auth_log()->debug("execute: ack body received ({} bytes), parsing", ack_body_len);

        // Store full wire ack = len_prefix(2) || ciphertext for MAC derivation
        // go-ethereum uses authRespPacket (full wire bytes) in secrets()
        ByteBuffer ack_wire_full;
        ack_wire_full.reserve(len_bytes.size() + ack_result.value().size());
        ack_wire_full.insert(ack_wire_full.end(), len_bytes.begin(), len_bytes.end());
        ack_wire_full.insert(ack_wire_full.end(), ack_result.value().begin(), ack_result.value().end());
        result.key_material.recipient_ack_message = ack_wire_full;

        // Pass the 2-byte ack prefix as shared_mac_data so ECIES MAC matches
        auto parse_result = parse_ack_message(
            ByteView(ack_result.value().data(), ack_result.value().size()),
            config_.local_private_key,
            len_bytes,
            result.key_material);
        if ( !parse_result ) {
            auth_log()->debug("execute: parse_ack_message failed (code {})",
                              static_cast<int>(parse_result.error()));
            return SessionError::kAuthenticationFailed;
        }
        auth_log()->debug("execute: ack parsed successfully");

        result.key_material.peer_public_key = config_.peer_public_key.value();
    }
    else
    {
        // ── Responder: receive auth ─────────────────────────────────────────
        result.key_material.recipient_nonce = local_nonce;

        // Read 2-byte length prefix
        auto len_result = transport_.read_exact(kEip8LengthPrefixSize, yield);
        if ( !len_result ) {
            return SessionError::kAuthenticationFailed;
        }
        const auto& len_bytes = len_result.value();
        const size_t auth_len = (static_cast<size_t>(len_bytes[0]) << 8U)
                              |  static_cast<size_t>(len_bytes[1]);
        if (auth_len > kMaxEip8HandshakePacketSize) {
            auth_log()->debug("execute: peer {}:{} pubkey={} auth length {} exceeds EIP-8 max {}",
                              remote_addr,
                              remote_port,
                              remote_pubkey_hex,
                              auth_len,
                              kMaxEip8HandshakePacketSize);
            return SessionError::kAuthenticationFailed;
        }

        auto auth_result = transport_.read_exact(auth_len, yield);
        if ( !auth_result ) {
            return SessionError::kAuthenticationFailed;
        }

        auto parse_result = parse_auth_message(
            ByteView(auth_result.value().data(), auth_result.value().size()),
            config_.local_private_key,
            len_bytes);
        if ( !parse_result ) {
            return SessionError::kAuthenticationFailed;
        }
        result.key_material = parse_result.value();
        result.key_material.initiator_auth_message   = auth_result.value();
        result.key_material.local_ephemeral_public_key  = keypair.public_key;
        result.key_material.local_ephemeral_private_key = keypair.private_key;
        result.key_material.recipient_nonce             = local_nonce;

        // ── Responder: send ack ─────────────────────────────────────────────
        auto ack_msg_result = create_ack_message(
            keypair.public_key,
            local_nonce,
            result.key_material.peer_public_key
        );
        if ( !ack_msg_result ) {
            return SessionError::kAuthenticationFailed;
        }
        result.key_material.recipient_ack_message = ack_msg_result.value();

        const auto& ack_bytes = result.key_material.recipient_ack_message;
        const auto  ack_len   = static_cast<uint16_t>(ack_bytes.size());
        ByteBuffer  ack_wire;
        ack_wire.reserve(kEip8LengthPrefixSize + ack_bytes.size());
        ack_wire.push_back(static_cast<uint8_t>(ack_len >> 8U));
        ack_wire.push_back(static_cast<uint8_t>(ack_len & 0xFFU));
        ack_wire.insert(ack_wire.end(), ack_bytes.begin(), ack_bytes.end());

        auto send_result = transport_.write_all(
            ByteView(ack_wire.data(), ack_wire.size()), yield);
        if ( !send_result ) {
            return SessionError::kAuthenticationFailed;
        }
    }

    // ── Derive frame secrets ────────────────────────────────────────────────
    result.frame_secrets = derive_frame_secrets(result.key_material, is_initiator());

    // ── Hand transport back to caller via HandshakeResult ───────────────────
    result.transport = std::move(transport_);

    return result;
}

AuthResult<AuthKeyMaterial> AuthHandshake::perform_auth(asio::yield_context /*yield*/) noexcept {
    // This method would contain the core auth logic
    // Currently integrated into execute() above
    return AuthError::kInvalidAuthMessage; // Placeholder
}

Result<void> AuthHandshake::exchange_hello(
    ByteView aes_key,
    ByteView mac_key,
    asio::yield_context /*yield*/
) noexcept {
    (void)aes_key;
    (void)mac_key;
    // This would perform the Hello message exchange
    // Placeholder for now
    return SessionError::kHandshakeFailed;
}

FrameSecrets AuthHandshake::derive_frame_secrets(
    const AuthKeyMaterial& keys,
    bool is_initiator
) noexcept {
    FrameSecrets secrets;

    // ── Step 1: ECDH between local ephemeral private key and peer ephemeral public key ──
    // go-ethereum: ecdheSecret = randomPrivKey.GenerateShared(remoteRandomPub, 16, 16)
    // Returns raw x-coordinate (32 bytes), same as our compute_shared_secret.
    auto ecdhe_result = rlpx::crypto::Ecdh::compute_shared_secret(
        keys.peer_ephemeral_public_key,
        keys.local_ephemeral_private_key
    );
    if (!ecdhe_result) { return secrets; }
    const auto& ecdhe_secret = ecdhe_result.value();  // 32-byte x-coordinate

    // ── Step 2: sharedSecret = keccak256(ecdheSecret || keccak256(respNonce || initNonce)) ──
    // go-ethereum: keccak256(h.respNonce, h.initNonce)
    std::array<uint8_t, kNonceSize> nonce_hash{};
    {
        using Hasher = nil::crypto3::hashes::keccak_1600<256>;
        nil::crypto3::accumulator_set<Hasher> acc;
        nil::crypto3::hash<Hasher>(keys.recipient_nonce.begin(), keys.recipient_nonce.end(), acc);
        nil::crypto3::hash<Hasher>(keys.initiator_nonce.begin(), keys.initiator_nonce.end(), acc);
        auto digest = nil::crypto3::accumulators::extract::hash<Hasher>(acc);
        std::copy(digest.begin(), digest.end(), nonce_hash.begin());
    }

    std::array<uint8_t, kNonceSize> shared_secret{};
    {
        using Hasher = nil::crypto3::hashes::keccak_1600<256>;
        nil::crypto3::accumulator_set<Hasher> acc;
        nil::crypto3::hash<Hasher>(ecdhe_secret.begin(), ecdhe_secret.end(), acc);
        nil::crypto3::hash<Hasher>(nonce_hash.begin(), nonce_hash.end(), acc);
        auto digest = nil::crypto3::accumulators::extract::hash<Hasher>(acc);
        std::copy(digest.begin(), digest.end(), shared_secret.begin());
    }

    // ── Step 3: aesSecret = keccak256(ecdheSecret || sharedSecret) ──
    std::array<uint8_t, kNonceSize> aes_secret{};
    {
        using Hasher = nil::crypto3::hashes::keccak_1600<256>;
        nil::crypto3::accumulator_set<Hasher> acc;
        nil::crypto3::hash<Hasher>(ecdhe_secret.begin(), ecdhe_secret.end(), acc);
        nil::crypto3::hash<Hasher>(shared_secret.begin(), shared_secret.end(), acc);
        auto digest = nil::crypto3::accumulators::extract::hash<Hasher>(acc);
        std::copy(digest.begin(), digest.end(), aes_secret.begin());
    }
    std::copy(aes_secret.begin(), aes_secret.end(), secrets.aes_secret.begin());

    // ── Step 4: mac_secret = keccak256(ecdheSecret || aesSecret) ──
    std::array<uint8_t, kNonceSize> mac_secret{};
    {
        using Hasher = nil::crypto3::hashes::keccak_1600<256>;
        nil::crypto3::accumulator_set<Hasher> acc;
        nil::crypto3::hash<Hasher>(ecdhe_secret.begin(), ecdhe_secret.end(), acc);
        nil::crypto3::hash<Hasher>(aes_secret.begin(), aes_secret.end(), acc);
        auto digest = nil::crypto3::accumulators::extract::hash<Hasher>(acc);
        std::copy(digest.begin(), digest.end(), mac_secret.begin());
    }
    std::copy(mac_secret.begin(), mac_secret.end(), secrets.mac_secret.begin());

    // ── Step 5: MAC seeds ──
    // go-ethereum:
    //   mac1.Write(xor(MAC, respNonce)); mac1.Write(auth)     // = egress for initiator
    //   mac2.Write(xor(MAC, initNonce)); mac2.Write(authResp) // = ingress for initiator
    // We store the full running-keccak state as a seed byte string.

    auto mac_seed_bytes = [&](const std::array<uint8_t, kNonceSize>& nonce,
                               const ByteBuffer& msg) -> ByteBuffer
    {
        // xor_val = mac_secret XOR nonce
        std::array<uint8_t, kNonceSize> xor_val{};
        for (size_t i = 0; i < kNonceSize; ++i)
        {
            xor_val[i] = mac_secret[i] ^ nonce[i];
        }
        // Return raw bytes: xor_val || msg
        // go-ethereum: mac.Write(xor_val); mac.Write(msg)
        ByteBuffer seed;
        seed.reserve(kNonceSize + msg.size());
        seed.insert(seed.end(), xor_val.begin(), xor_val.end());
        seed.insert(seed.end(), msg.begin(),     msg.end());
        return seed;
    };

    if (is_initiator)
    {
        // egress MAC: mac1.Write(xor(mac,respNonce)); mac1.Write(auth)
        secrets.egress_mac_seed  = mac_seed_bytes(keys.recipient_nonce,
                                                   keys.initiator_auth_message);
        // ingress MAC: mac2.Write(xor(mac,initNonce)); mac2.Write(authResp)
        secrets.ingress_mac_seed = mac_seed_bytes(keys.initiator_nonce,
                                                   keys.recipient_ack_message);
    }
    else
    {
        // Responder: roles swapped
        secrets.ingress_mac_seed = mac_seed_bytes(keys.recipient_nonce,
                                                   keys.initiator_auth_message);
        secrets.egress_mac_seed  = mac_seed_bytes(keys.initiator_nonce,
                                                   keys.recipient_ack_message);
    }

    auth_log()->debug("derive_frame_secrets: aes_secret[0..3]={:02x}{:02x}{:02x}{:02x}",
                      secrets.aes_secret[0], secrets.aes_secret[1],
                      secrets.aes_secret[2], secrets.aes_secret[3]);
    auth_log()->debug("derive_frame_secrets: mac_secret[0..3]={:02x}{:02x}{:02x}{:02x}",
                      secrets.mac_secret[0], secrets.mac_secret[1],
                      secrets.mac_secret[2], secrets.mac_secret[3]);
    auth_log()->debug("derive_frame_secrets: egress_seed[0..3]={:02x}{:02x}{:02x}{:02x} len={}",
                      secrets.egress_mac_seed[0], secrets.egress_mac_seed[1],
                      secrets.egress_mac_seed[2], secrets.egress_mac_seed[3],
                      secrets.egress_mac_seed.size());
    auth_log()->debug("derive_frame_secrets: ingress_seed[0..3]={:02x}{:02x}{:02x}{:02x} len={}",
                      secrets.ingress_mac_seed[0], secrets.ingress_mac_seed[1],
                      secrets.ingress_mac_seed[2], secrets.ingress_mac_seed[3],
                      secrets.ingress_mac_seed.size());
    auth_log()->debug("derive_frame_secrets: auth_wire len={}, ack_wire len={}",
                      keys.initiator_auth_message.size(), keys.recipient_ack_message.size());

    return secrets;
}

FrameSecrets derive_frame_secrets(const AuthKeyMaterial& keys, bool is_initiator) noexcept
{
    return AuthHandshake::derive_frame_secrets(keys, is_initiator);
}

} // namespace rlpx::auth
