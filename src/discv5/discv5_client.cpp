// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv5/discv5_client.hpp"
#include "discv5/discv5_constants.hpp"
#include "discv5/discv5_enr.hpp"

#include <rlp/rlp_decoder.hpp>
#include <rlp/rlp_encoder.hpp>
#include <rlpx/crypto/ecdh.hpp>

#include <boost/asio/spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>

#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <string_view>
#include <vector>

namespace discv5
{

namespace
{

using NodeAddress = std::array<uint8_t, kKeccak256Bytes>;

static constexpr size_t  kMessageAuthDataBytes = kKeccak256Bytes;

struct PacketView
{
    uint8_t               flag{};
    std::array<uint8_t, kGcmNonceBytes> nonce{};
    uint16_t              auth_size{};
    std::vector<uint8_t>  header_data{};  ///< IV + unmasked static header + unmasked auth data
    std::vector<uint8_t>  auth_data{};    ///< Unmasked auth data only
    std::vector<uint8_t>  msg_data{};     ///< Raw encrypted message bytes (not masking-XOR'd)
};

struct HandshakeAuthView
{
    NodeAddress           src_id{};
    std::vector<uint8_t>  signature{};
    std::vector<uint8_t>  pubkey{};
    std::vector<uint8_t>  record{};
};

NodeAddress derive_node_address(const NodeId& public_key) noexcept
{
    const auto hash_val =
        nil::crypto3::hash<nil::crypto3::hashes::keccak_1600<256>>(
            public_key.cbegin(), public_key.cend());
    return static_cast<NodeAddress>(hash_val);
}

std::string endpoint_key(const udp::endpoint& endpoint)
{
    return endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

std::string endpoint_key(const std::string& ip, uint16_t port)
{
    return ip + ":" + std::to_string(port);
}

uint16_t read_u16_be(const uint8_t* data) noexcept
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(data[0U]) << 8U) |
        static_cast<uint16_t>(data[1U]));
}

uint64_t read_u64_be(const uint8_t* data) noexcept
{
    uint64_t value = 0U;
    for (size_t i = 0U; i < sizeof(uint64_t); ++i)
    {
        value = static_cast<uint64_t>((value << 8U) | data[i]);
    }
    return value;
}

void append_u16_be(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

void append_u64_be(std::vector<uint8_t>& out, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFU));
    }
}

bool random_bytes(uint8_t* out, size_t size) noexcept
{
    return RAND_bytes(out, static_cast<int>(size)) == 1;
}

std::array<uint8_t, SHA256_DIGEST_LENGTH> sha256_bytes(const std::vector<uint8_t>& data) noexcept
{
    std::array<uint8_t, SHA256_DIGEST_LENGTH> digest{};
    SHA256(data.data(), data.size(), digest.data());
    return digest;
}

std::array<uint8_t, SHA256_DIGEST_LENGTH> hmac_sha256(
    const uint8_t* key,
    size_t key_size,
    const uint8_t* data,
    size_t data_size) noexcept
{
    std::array<uint8_t, SHA256_DIGEST_LENGTH> digest{};
    unsigned int digest_len = 0U;
    HMAC(
        EVP_sha256(),
        key,
        static_cast<int>(key_size),
        data,
        data_size,
        digest.data(),
        &digest_len);
    return digest;
}

Result<std::array<uint8_t, kPrivateKeyBytes>> hkdf_expand_32(
    const std::vector<uint8_t>& salt,
    const std::vector<uint8_t>& ikm,
    const std::vector<uint8_t>& info) noexcept
{
    const auto prk = hmac_sha256(salt.data(), salt.size(), ikm.data(), ikm.size());

    std::vector<uint8_t> t1_input;
    t1_input.reserve(info.size() + kMessageTypePrefixBytes);
    t1_input.insert(t1_input.end(), info.begin(), info.end());
    t1_input.push_back(kHkdfFirstBlockCounter);

    return hmac_sha256(prk.data(), prk.size(), t1_input.data(), t1_input.size());
}

Result<std::array<uint8_t, kCompressedKeyBytes>> compress_public_key(const NodeId& public_key) noexcept
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    std::array<uint8_t, kUncompressedKeyBytes> raw{};
    raw[0U] = kUncompressedPubKeyPrefix;
    std::copy(public_key.begin(), public_key.end(), raw.begin() + kUncompressedPubKeyDataOffset);

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, raw.data(), raw.size()))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrInvalidSecp256k1Key;
    }

    std::array<uint8_t, kCompressedKeyBytes> compressed{};
    size_t compressed_len = compressed.size();
    if (!secp256k1_ec_pubkey_serialize(
            ctx,
            compressed.data(),
            &compressed_len,
            &pubkey,
            SECP256K1_EC_COMPRESSED))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrInvalidSecp256k1Key;
    }

    secp256k1_context_destroy(ctx);
    return compressed;
}

Result<std::array<uint8_t, kCompressedKeyBytes>> shared_secret_from_uncompressed_pubkey(
    const NodeId& remote_node_id,
    const std::array<uint8_t, kPrivateKeyBytes>& private_key) noexcept
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    std::array<uint8_t, kUncompressedKeyBytes> raw{};
    raw[0U] = kUncompressedPubKeyPrefix;
    std::copy(remote_node_id.begin(), remote_node_id.end(), raw.begin() + kUncompressedPubKeyDataOffset);

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, raw.data(), raw.size()))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrInvalidSecp256k1Key;
    }

    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &pubkey, private_key.data()))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kContextCreationFailed;
    }

    std::array<uint8_t, kCompressedKeyBytes> shared{};
    size_t shared_len = shared.size();
    if (!secp256k1_ec_pubkey_serialize(
            ctx,
            shared.data(),
            &shared_len,
            &pubkey,
            SECP256K1_EC_COMPRESSED))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kContextCreationFailed;
    }

    secp256k1_context_destroy(ctx);
    return shared;
}

Result<std::array<uint8_t, kCompressedKeyBytes>> shared_secret_from_compressed_pubkey(
    const std::vector<uint8_t>& remote_pubkey,
    const std::array<uint8_t, kPrivateKeyBytes>& private_key) noexcept
{
    if (remote_pubkey.size() != kCompressedKeyBytes)
    {
        return discv5Error::kEnrInvalidSecp256k1Key;
    }

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, remote_pubkey.data(), remote_pubkey.size()))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrInvalidSecp256k1Key;
    }

    if (!secp256k1_ec_pubkey_tweak_mul(ctx, &pubkey, private_key.data()))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kContextCreationFailed;
    }

    std::array<uint8_t, kCompressedKeyBytes> shared{};
    size_t shared_len = shared.size();
    if (!secp256k1_ec_pubkey_serialize(
            ctx,
            shared.data(),
            &shared_len,
            &pubkey,
            SECP256K1_EC_COMPRESSED))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kContextCreationFailed;
    }

    secp256k1_context_destroy(ctx);
    return shared;
}

Result<std::vector<uint8_t>> make_id_signature(
    const std::array<uint8_t, kPrivateKeyBytes>& private_key,
    const std::vector<uint8_t>& challenge_data,
    const std::vector<uint8_t>& eph_pubkey,
    const NodeAddress& destination_node_addr) noexcept
{
    std::vector<uint8_t> input;
    static constexpr std::string_view kPrefix = "discovery v5 identity proof";
    input.reserve(kPrefix.size() + challenge_data.size() + eph_pubkey.size() + destination_node_addr.size());
    input.insert(input.end(), kPrefix.begin(), kPrefix.end());
    input.insert(input.end(), challenge_data.begin(), challenge_data.end());
    input.insert(input.end(), eph_pubkey.begin(), eph_pubkey.end());
    input.insert(input.end(), destination_node_addr.begin(), destination_node_addr.end());

    const auto digest = sha256_bytes(input);

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, digest.data(), private_key.data(), nullptr, nullptr))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kContextCreationFailed;
    }

    std::vector<uint8_t> compact(kEnrSigBytes);
    int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact.data(), &recid, &sig);
    secp256k1_context_destroy(ctx);
    return compact;
}

bool verify_id_signature(
    const NodeId& node_id,
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& challenge_data,
    const std::vector<uint8_t>& eph_pubkey,
    const NodeAddress& destination_node_addr) noexcept
{
    if (signature.size() != kEnrSigBytes)
    {
        return false;
    }

    std::vector<uint8_t> input;
    static constexpr std::string_view kPrefix = "discovery v5 identity proof";
    input.reserve(kPrefix.size() + challenge_data.size() + eph_pubkey.size() + destination_node_addr.size());
    input.insert(input.end(), kPrefix.begin(), kPrefix.end());
    input.insert(input.end(), challenge_data.begin(), challenge_data.end());
    input.insert(input.end(), eph_pubkey.begin(), eph_pubkey.end());
    input.insert(input.end(), destination_node_addr.begin(), destination_node_addr.end());

    const auto digest = sha256_bytes(input);

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (ctx == nullptr)
    {
        return false;
    }

    std::array<uint8_t, kUncompressedKeyBytes> raw{};
    raw[0U] = kUncompressedPubKeyPrefix;
    std::copy(node_id.begin(), node_id.end(), raw.begin() + kUncompressedPubKeyDataOffset);

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, raw.data(), raw.size()))
    {
        secp256k1_context_destroy(ctx);
        return false;
    }

    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_signature_parse_compact(ctx, &sig, signature.data()))
    {
        secp256k1_context_destroy(ctx);
        return false;
    }

    const bool verified = secp256k1_ecdsa_verify(ctx, &sig, digest.data(), &pubkey) == 1;
    secp256k1_context_destroy(ctx);
    return verified;
}

Result<std::pair<std::array<uint8_t, kAes128KeyBytes>, std::array<uint8_t, kAes128KeyBytes>>> derive_session_keys(
    const std::array<uint8_t, kCompressedKeyBytes>& shared_secret,
    const std::vector<uint8_t>& challenge_data,
    const NodeAddress& first_id,
    const NodeAddress& second_id) noexcept
{
    static constexpr std::string_view kInfoPrefix = "discovery v5 key agreement";

    std::vector<uint8_t> info;
    info.reserve(kInfoPrefix.size() + first_id.size() + second_id.size());
    info.insert(info.end(), kInfoPrefix.begin(), kInfoPrefix.end());
    info.insert(info.end(), first_id.begin(), first_id.end());
    info.insert(info.end(), second_id.begin(), second_id.end());

    std::vector<uint8_t> ikm(shared_secret.begin(), shared_secret.end());
    auto okm_result = hkdf_expand_32(challenge_data, ikm, info);
    if (!okm_result)
    {
        return okm_result.error();
    }

    std::array<uint8_t, kAes128KeyBytes> write_key{};
    std::array<uint8_t, kAes128KeyBytes> read_key{};
    const auto& okm = okm_result.value();
    std::copy_n(okm.begin(), write_key.size(), write_key.begin());
    std::copy_n(okm.begin() + write_key.size(), read_key.size(), read_key.begin());
    return std::make_pair(write_key, read_key);
}

bool apply_aes128_ctr(
    const std::array<uint8_t, kAes128KeyBytes>& key,
    const std::array<uint8_t, kMaskingIvBytes>& iv,
    uint8_t* out,
    const uint8_t* in,
    size_t size) noexcept
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
    {
        return false;
    }

    const int init_ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data());
    if (init_ok != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    int out_len = 0;
    const int update_ok = EVP_EncryptUpdate(ctx, out, &out_len, in, static_cast<int>(size));
    EVP_CIPHER_CTX_free(ctx);
    return update_ok == 1 && out_len == static_cast<int>(size);
}

Result<std::vector<uint8_t>> encrypt_gcm(
    const std::array<uint8_t, kAes128KeyBytes>& key,
    const std::array<uint8_t, kGcmNonceBytes>& nonce,
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& auth_data) noexcept
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    std::vector<uint8_t> ciphertext(plaintext.size() + kGcmTagBytes);
    int out_len = 0;
    int total_len = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kGcmNonceBytes, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kContextCreationFailed;
    }

    if (!auth_data.empty())
    {
        if (EVP_EncryptUpdate(ctx, nullptr, &out_len, auth_data.data(), static_cast<int>(auth_data.size())) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return discv5Error::kContextCreationFailed;
        }
    }

    if (!plaintext.empty())
    {
        if (EVP_EncryptUpdate(
                ctx,
                ciphertext.data(),
                &out_len,
                plaintext.data(),
                static_cast<int>(plaintext.size())) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return discv5Error::kContextCreationFailed;
        }
        total_len += out_len;
    }

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + total_len, &out_len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kContextCreationFailed;
    }
    total_len += out_len;

    if (EVP_CIPHER_CTX_ctrl(
            ctx,
            EVP_CTRL_GCM_GET_TAG,
            kGcmTagBytes,
            ciphertext.data() + total_len) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kContextCreationFailed;
    }

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(static_cast<size_t>(total_len) + kGcmTagBytes);
    return ciphertext;
}

Result<std::vector<uint8_t>> decrypt_gcm(
    const std::array<uint8_t, kAes128KeyBytes>& key,
    const std::array<uint8_t, kGcmNonceBytes>& nonce,
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& auth_data) noexcept
{
    if (ciphertext.size() < kGcmTagBytes)
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    const size_t text_size = ciphertext.size() - kGcmTagBytes;
    std::vector<uint8_t> plaintext(text_size);
    int out_len = 0;
    int total_len = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kGcmNonceBytes, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kContextCreationFailed;
    }

    if (!auth_data.empty())
    {
        if (EVP_DecryptUpdate(ctx, nullptr, &out_len, auth_data.data(), static_cast<int>(auth_data.size())) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return discv5Error::kContextCreationFailed;
        }
    }

    if (text_size > 0U)
    {
        if (EVP_DecryptUpdate(
                ctx,
                plaintext.data(),
                &out_len,
                ciphertext.data(),
                static_cast<int>(text_size)) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return discv5Error::kNetworkReceiveFailed;
        }
        total_len += out_len;
    }

    if (EVP_CIPHER_CTX_ctrl(
            ctx,
            EVP_CTRL_GCM_SET_TAG,
            kGcmTagBytes,
            const_cast<uint8_t*>(ciphertext.data() + text_size)) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kNetworkReceiveFailed;
    }

    const int final_ok = EVP_DecryptFinal_ex(ctx, plaintext.data() + total_len, &out_len);
    EVP_CIPHER_CTX_free(ctx);
    if (final_ok != 1)
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    total_len += out_len;
    plaintext.resize(static_cast<size_t>(total_len));
    return plaintext;
}

Result<PacketView> decode_packet(
    const uint8_t* data,
    size_t length,
    const NodeAddress& destination_node_addr) noexcept
{
    if (length < kStaticPacketBytes)
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    PacketView packet;
    packet.header_data.resize(kStaticPacketBytes);
    std::copy(data, data + kMaskingIvBytes, packet.header_data.begin());

    std::array<uint8_t, kMaskingIvBytes> key{};
    std::copy_n(destination_node_addr.begin(), key.size(), key.begin());
    std::array<uint8_t, kMaskingIvBytes> iv{};
    std::copy_n(data, iv.size(), iv.begin());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kContextCreationFailed;
    }

    int out_len = 0;
    if (EVP_EncryptUpdate(
            ctx,
            packet.header_data.data() + kMaskingIvBytes,
            &out_len,
            data + kMaskingIvBytes,
            static_cast<int>(kStaticHeaderBytes)) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kNetworkReceiveFailed;
    }

    const uint8_t* static_header = packet.header_data.data() + kMaskingIvBytes;
    if (std::memcmp(static_header, kProtocolId, kProtocolIdBytes) != 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kNetworkReceiveFailed;
    }

    const uint16_t version = read_u16_be(static_header + kProtocolIdBytes);
    if (version < kProtocolVersion)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kNetworkReceiveFailed;
    }

    packet.flag = static_header[kStaticHeaderFlagOffset];
    std::copy_n(
        static_header + kStaticHeaderNonceOffset,
        packet.nonce.size(),
        packet.nonce.begin());
    packet.auth_size = read_u16_be(
        static_header + kStaticHeaderAuthSizeOffset);

    const size_t auth_end = kStaticPacketBytes + packet.auth_size;
    if (auth_end > length)
    {
        EVP_CIPHER_CTX_free(ctx);
        return discv5Error::kNetworkReceiveFailed;
    }

    packet.auth_data.resize(packet.auth_size);
    packet.header_data.resize(auth_end);
    if (packet.auth_size > 0U)
    {
        if (EVP_EncryptUpdate(
                ctx,
                packet.auth_data.data(),
                &out_len,
                data + kStaticPacketBytes,
                static_cast<int>(packet.auth_size)) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            return discv5Error::kNetworkReceiveFailed;
        }
        std::copy(packet.auth_data.begin(), packet.auth_data.end(), packet.header_data.begin() + kStaticPacketBytes);
    }

    EVP_CIPHER_CTX_free(ctx);
    packet.msg_data.assign(data + auth_end, data + length);
    return packet;
}

Result<std::vector<uint8_t>> encode_packet(
    uint8_t flag,
    const std::array<uint8_t, kGcmNonceBytes>& nonce,
    const std::vector<uint8_t>& auth_data,
    const std::vector<uint8_t>& msg_data,
    const NodeAddress& destination_node_addr,
    std::vector<uint8_t>* unmasked_header_out = nullptr) noexcept
{
    std::vector<uint8_t> packet;
    packet.reserve(kStaticPacketBytes + auth_data.size() + msg_data.size());

    std::array<uint8_t, kMaskingIvBytes> iv{};
    if (!random_bytes(iv.data(), iv.size()))
    {
        return discv5Error::kNetworkSendFailed;
    }

    packet.insert(packet.end(), iv.begin(), iv.end());
    packet.insert(packet.end(), kProtocolId, kProtocolId + kProtocolIdBytes);
    append_u16_be(packet, kProtocolVersion);
    packet.push_back(flag);
    packet.insert(packet.end(), nonce.begin(), nonce.end());
    append_u16_be(packet, static_cast<uint16_t>(auth_data.size()));
    packet.insert(packet.end(), auth_data.begin(), auth_data.end());

    if (unmasked_header_out != nullptr)
    {
        *unmasked_header_out = packet;
    }

    std::array<uint8_t, kAes128KeyBytes> key{};
    std::copy_n(destination_node_addr.begin(), key.size(), key.begin());
    if (!apply_aes128_ctr(
            key,
            iv,
            packet.data() + kMaskingIvBytes,
            packet.data() + kMaskingIvBytes,
            packet.size() - kMaskingIvBytes))
    {
        return discv5Error::kNetworkSendFailed;
    }

    packet.insert(packet.end(), msg_data.begin(), msg_data.end());
    return packet;
}

std::vector<uint8_t> make_message_auth_data(const NodeAddress& local_node_addr)
{
    return std::vector<uint8_t>(local_node_addr.begin(), local_node_addr.end());
}

Result<std::vector<uint8_t>> make_local_enr_record(const discv5Config& config, uint16_t udp_port) noexcept
{
    const auto compressed_result = compress_public_key(config.public_key);
    if (!compressed_result)
    {
        return compressed_result.error();
    }

    boost::system::error_code ec;
    const asio::ip::address bind_addr = asio::ip::make_address(config.bind_ip, ec);
    if (ec)
    {
        return discv5Error::kEnrInvalidIp;
    }

    std::vector<uint8_t> ip_bytes;
    if (bind_addr.is_v4())
    {
        const auto bytes = bind_addr.to_v4().to_bytes();
        ip_bytes.assign(bytes.begin(), bytes.end());
    }
    else
    {
        ip_bytes = std::vector<uint8_t>(kIPv4Bytes, 0U);
    }

    const uint16_t tcp_port = (config.tcp_port != 0U) ? config.tcp_port : udp_port;

    rlp::RlpEncoder content_enc;
    if (!content_enc.BeginList() ||
        !content_enc.add(static_cast<uint64_t>(kInitialEnrSeq)) ||
        !content_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyId), kEnrKeyIdBytes)) ||
        !content_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kIdentitySchemeV4), kIdentitySchemeV4Bytes)) ||
        !content_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyIp), kEnrKeyIpBytes)) ||
        !content_enc.add(rlp::ByteView(ip_bytes.data(), ip_bytes.size())) ||
        !content_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeySecp256k1), kEnrKeySecp256k1Bytes)) ||
        !content_enc.add(rlp::ByteView(compressed_result.value().data(), compressed_result.value().size())) ||
        !content_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyTcp), kEnrKeyTcpBytes)) ||
        !content_enc.add(tcp_port) ||
        !content_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyUdp), kEnrKeyUdpBytes)) ||
        !content_enc.add(udp_port) ||
        !content_enc.EndList())
    {
        return discv5Error::kEnrRlpDecodeFailed;
    }

    auto content_bytes_result = content_enc.MoveBytes();
    if (!content_bytes_result)
    {
        return discv5Error::kEnrRlpDecodeFailed;
    }

    const rlp::Bytes& content_bytes = content_bytes_result.value();
    const auto content_hash = nil::crypto3::hash<nil::crypto3::hashes::keccak_1600<256>>(
        content_bytes.cbegin(), content_bytes.cend());
    const std::array<uint8_t, kKeccak256Bytes> content_hash_bytes = content_hash;

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(
            ctx,
            &sig,
            content_hash_bytes.data(),
            config.private_key.data(),
            nullptr,
            nullptr))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kContextCreationFailed;
    }

    std::array<uint8_t, kEnrSigBytes> compact_sig{};
    int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact_sig.data(), &recid, &sig);
    secp256k1_context_destroy(ctx);

    rlp::RlpEncoder full_enc;
    if (!full_enc.BeginList() ||
        !full_enc.add(rlp::ByteView(compact_sig.data(), compact_sig.size())) ||
        !full_enc.add(static_cast<uint64_t>(kInitialEnrSeq)) ||
        !full_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyId), kEnrKeyIdBytes)) ||
        !full_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kIdentitySchemeV4), kIdentitySchemeV4Bytes)) ||
        !full_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyIp), kEnrKeyIpBytes)) ||
        !full_enc.add(rlp::ByteView(ip_bytes.data(), ip_bytes.size())) ||
        !full_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeySecp256k1), kEnrKeySecp256k1Bytes)) ||
        !full_enc.add(rlp::ByteView(compressed_result.value().data(), compressed_result.value().size())) ||
        !full_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyTcp), kEnrKeyTcpBytes)) ||
        !full_enc.add(tcp_port) ||
        !full_enc.add(rlp::ByteView(reinterpret_cast<const uint8_t*>(kEnrKeyUdp), kEnrKeyUdpBytes)) ||
        !full_enc.add(udp_port) ||
        !full_enc.EndList())
    {
        return discv5Error::kEnrRlpDecodeFailed;
    }

    auto full_bytes_result = full_enc.MoveBytes();
    if (!full_bytes_result)
    {
        return discv5Error::kEnrRlpDecodeFailed;
    }

    const rlp::Bytes& full_bytes = full_bytes_result.value();
    return std::vector<uint8_t>(full_bytes.begin(), full_bytes.end());
}

Result<std::vector<uint8_t>> make_findnode_plaintext(const std::vector<uint8_t>& req_id) noexcept
{
    rlp::RlpEncoder enc;
    if (!enc.BeginList())
    {
        return discv5Error::kNetworkSendFailed;
    }

    if (!enc.add(rlp::ByteView(req_id.data(), req_id.size())))
    {
        return discv5Error::kNetworkSendFailed;
    }

    if (!enc.BeginList() ||
        !enc.add(static_cast<uint32_t>(kFindNodeDistanceAll)) ||
        !enc.EndList() ||
        !enc.EndList())
    {
        return discv5Error::kNetworkSendFailed;
    }

    auto bytes_result = enc.MoveBytes();
    if (!bytes_result)
    {
        return discv5Error::kNetworkSendFailed;
    }

    std::vector<uint8_t> plaintext;
    plaintext.reserve(kMessageTypePrefixBytes + bytes_result.value().size());
    plaintext.push_back(kMsgFindNode);
    plaintext.insert(plaintext.end(), bytes_result.value().begin(), bytes_result.value().end());
    return plaintext;
}

Result<std::vector<uint8_t>> make_nodes_plaintext(
    const std::vector<uint8_t>& req_id,
    const std::vector<uint8_t>& enr_record) noexcept
{
    rlp::RlpEncoder enc;
    if (!enc.BeginList() ||
        !enc.add(rlp::ByteView(req_id.data(), req_id.size())) ||!enc.add(kNodesResponseCountSingle) ||
        !enc.BeginList() ||
        !enc.AddRaw(rlp::ByteView(enr_record.data(), enr_record.size())) ||
        !enc.EndList() ||
        !enc.EndList())
    {
        return discv5Error::kNetworkSendFailed;
    }

    auto bytes_result = enc.MoveBytes();
    if (!bytes_result)
    {
        return discv5Error::kNetworkSendFailed;
    }

    std::vector<uint8_t> plaintext;
    plaintext.reserve(kMessageTypePrefixBytes + bytes_result.value().size());
    plaintext.push_back(kMsgNodes);
    plaintext.insert(plaintext.end(), bytes_result.value().begin(), bytes_result.value().end());
    return plaintext;
}

Result<std::vector<uint8_t>> parse_findnode_req_id(const std::vector<uint8_t>& body) noexcept
{
    rlp::RlpDecoder decoder(rlp::ByteView(body.data(), body.size()));
    auto outer_len = decoder.ReadListHeaderBytes();
    if (!outer_len)
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    rlp::Bytes req_id;
    if (!decoder.read(req_id))
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    return std::vector<uint8_t>(req_id.begin(), req_id.end());
}

Result<std::pair<std::vector<uint8_t>, std::vector<ValidatedPeer>>> parse_nodes_message(const std::vector<uint8_t>& body) noexcept
{
    rlp::RlpDecoder decoder(rlp::ByteView(body.data(), body.size()));
    auto outer_len = decoder.ReadListHeaderBytes();
    if (!outer_len)
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    rlp::Bytes req_id;
    if (!decoder.read(req_id))
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    uint8_t resp_count = 0U;
    if (!decoder.read(resp_count))
    {
        return discv5Error::kNetworkReceiveFailed;
    }
    (void)resp_count;

    auto nodes_len_result = decoder.ReadListHeaderBytes();
    if (!nodes_len_result)
    {
        return discv5Error::kNetworkReceiveFailed;
    }
    const size_t nodes_len = nodes_len_result.value();
    const rlp::ByteView nodes_start = decoder.Remaining();

    std::vector<ValidatedPeer> peers;
    while (!decoder.IsFinished())
    {
        const size_t consumed = nodes_start.size() - decoder.Remaining().size();
        if (consumed >= nodes_len)
        {
            break;
        }

        auto header_result = decoder.PeekHeader();
        if (!header_result)
        {
            break;
        }

        const auto& header = header_result.value();
        const size_t raw_len = header.header_size_bytes + header.payload_size_bytes;
        const rlp::ByteView raw_item = decoder.Remaining().substr(0U, raw_len);
        std::vector<uint8_t> raw_record(raw_item.begin(), raw_item.end());

        auto skip_result = decoder.SkipItem();
        if (!skip_result)
        {
            break;
        }

        auto record_result = EnrParser::decode_rlp(raw_record);
        if (!record_result)
        {
            continue;
        }

        auto verify_result = EnrParser::verify_signature(record_result.value());
        if (!verify_result)
        {
            continue;
        }

        auto peer_result = EnrParser::to_validated_peer(record_result.value());
        if (!peer_result)
        {
            continue;
        }

        peers.push_back(peer_result.value());
    }

    return std::make_pair(std::vector<uint8_t>(req_id.begin(), req_id.end()), peers);
}

Result<HandshakeAuthView> parse_handshake_auth(const std::vector<uint8_t>& auth_data) noexcept
{
    if (auth_data.size() < kHandshakeAuthFixedBytes)
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    HandshakeAuthView view;
    std::copy_n(auth_data.begin(), view.src_id.size(), view.src_id.begin());
    const uint8_t sig_size = auth_data[kHandshakeAuthSigSizeOffset];
    const uint8_t pubkey_size = auth_data[kHandshakeAuthPubkeySizeOffset];

    const size_t key_offset = kHandshakeAuthFixedBytes;
    const size_t pubkey_offset = key_offset + sig_size;
    const size_t record_offset = pubkey_offset + pubkey_size;
    if (record_offset > auth_data.size())
    {
        return discv5Error::kNetworkReceiveFailed;
    }

    view.signature.assign(auth_data.begin() + key_offset, auth_data.begin() + pubkey_offset);
    view.pubkey.assign(auth_data.begin() + pubkey_offset, auth_data.begin() + record_offset);
    view.record.assign(auth_data.begin() + record_offset, auth_data.end());
    return view;
}

} // anonymous namespace

 // ---------------------------------------------------------------------------
 // Constructor / Destructor
 // ---------------------------------------------------------------------------

 discv5_client::discv5_client(asio::io_context& io_context, const discv5Config& config)
     : io_context_(io_context)
     , config_(config)
     , socket_(io_context, udp::endpoint(udp::v4(), config.bind_port))
     , crawler_(config)
 {
 }

 discv5_client::~discv5_client()
 {
     stop();
 }

 // ---------------------------------------------------------------------------
 // add_bootnode
 // ---------------------------------------------------------------------------

 void discv5_client::add_bootnode(const std::string& enr_uri) noexcept
 {
     config_.bootstrap_enrs.push_back(enr_uri);
 }

 // ---------------------------------------------------------------------------
 // set_peer_discovered_callback / set_error_callback
 // ---------------------------------------------------------------------------

 void discv5_client::set_peer_discovered_callback(PeerDiscoveredCallback callback) noexcept
 {
     crawler_.set_peer_discovered_callback(std::move(callback));
 }

 void discv5_client::set_error_callback(ErrorCallback callback) noexcept
 {
     crawler_.set_error_callback(std::move(callback));
 }

 // ---------------------------------------------------------------------------
 // start
 // ---------------------------------------------------------------------------

 VoidResult discv5_client::start() noexcept
 {
     if (running_.exchange(true))
     {
         return rlp::outcome::success();  // Idempotent: already running
     }

     // Start the receive loop on the io_context.
     asio::spawn(io_context_, [this](asio::yield_context yield)
     {
         receive_loop(yield);
     });

     // Start the crawler loop.
     asio::spawn(io_context_, [this](asio::yield_context yield)
     {
         crawler_loop(yield);
     });

     // Seed the crawler with configured bootstrap entries.
     auto crawler_start = crawler_.start();
     if (!crawler_start)
     {
         logger_->warn("discv5_client: crawler start returned: {}",
                       to_string(crawler_start.error()));
     }

     logger_->info("discv5_client started on port {}", bound_port());
     return rlp::outcome::success();
 }

 // ---------------------------------------------------------------------------
 // stop
 // ---------------------------------------------------------------------------

 void discv5_client::stop() noexcept
 {
     if (!running_.exchange(false))
     {
         return;
     }

     boost::system::error_code ec;
     socket_.close(ec);
     if (ec)
     {
         logger_->warn("discv5_client: socket close error: {}", ec.message());
     }

     auto stop_result = crawler_.stop();
     (void)stop_result;
 }

 // ---------------------------------------------------------------------------
 // stats / local_node_id
 // ---------------------------------------------------------------------------

 CrawlerStats discv5_client::stats() const noexcept
 {
     return crawler_.stats();
 }

 const NodeId& discv5_client::local_node_id() const noexcept
 {
     return config_.public_key;
 }

 bool discv5_client::is_running() const noexcept
 {
     return running_.load();
 }

 uint16_t discv5_client::bound_port() const noexcept
 {
     boost::system::error_code ec;
     const auto endpoint = socket_.local_endpoint(ec);
     if (ec)
     {
         return 0U;
     }

     return endpoint.port();
 }

 size_t discv5_client::received_packet_count() const noexcept
 {
     return received_packets_.load();
 }

 size_t discv5_client::dropped_undersized_packet_count() const noexcept
 {
     return dropped_undersized_packets_.load();
 }

 size_t discv5_client::send_findnode_failure_count() const noexcept
 {
     return send_findnode_failures_.load();
 }

 size_t discv5_client::whoareyou_packet_count() const noexcept
 {
     return whoareyou_packets_.load();
 }

size_t discv5_client::handshake_packet_count() const noexcept
{
    return handshake_packets_.load();
}

size_t discv5_client::outbound_handshake_attempt_count() const noexcept
{
    return outbound_handshake_attempts_.load();
}

size_t discv5_client::outbound_handshake_failure_count() const noexcept
{
    return outbound_handshake_failures_.load();
}

size_t discv5_client::inbound_handshake_reject_auth_count() const noexcept
{
    return inbound_handshake_reject_auth_.load();
}

size_t discv5_client::inbound_handshake_reject_challenge_count() const noexcept
{
    return inbound_handshake_reject_challenge_.load();
}

size_t discv5_client::inbound_handshake_reject_record_count() const noexcept
{
    return inbound_handshake_reject_record_.load();
}

size_t discv5_client::inbound_handshake_reject_crypto_count() const noexcept
{
    return inbound_handshake_reject_crypto_.load();
}

size_t discv5_client::inbound_handshake_reject_decrypt_count() const noexcept
{
    return inbound_handshake_reject_decrypt_.load();
}

size_t discv5_client::inbound_handshake_seen_count() const noexcept
{
    return inbound_handshake_seen_.load();
}

size_t discv5_client::inbound_message_seen_count() const noexcept
{
    return inbound_message_seen_.load();
}

size_t discv5_client::inbound_message_decrypt_fail_count() const noexcept
{
    return inbound_message_decrypt_fail_.load();
}

size_t discv5_client::nodes_packet_count() const noexcept
{
    return nodes_packets_.load();
}

 // ---------------------------------------------------------------------------
 // receive_loop
 // ---------------------------------------------------------------------------

 void discv5_client::receive_loop(asio::yield_context yield)
 {
     // Receive buffer sized to the maximum valid discv5 packet.
     std::vector<uint8_t> buf(kMaxPacketBytes);

     while (running_.load())
     {
         udp::endpoint sender;
         boost::system::error_code ec;

         const size_t received = socket_.async_receive_from(
             asio::buffer(buf),
             sender,
             asio::redirect_error(yield, ec));

         if (ec)
         {
             if (!running_.load())
             {
                 break;  // Normal shutdown
             }
             logger_->warn("discv5 recv error: {}", ec.message());
             continue;
         }

         if (received < kMinPacketBytes)
         {
             ++dropped_undersized_packets_;
             logger_->debug("discv5: dropping undersized packet ({} bytes) from {}",
                            received, sender.address().to_string());
             continue;
         }

         ++received_packets_;
         handle_packet(buf.data(), received, sender);
     }
 }

 // ---------------------------------------------------------------------------
 // crawler_loop
 // ---------------------------------------------------------------------------

 void discv5_client::crawler_loop(asio::yield_context yield)
 {
     const auto interval = std::chrono::seconds(config_.query_interval_sec);
     asio::steady_timer timer(io_context_);

     while (running_.load())
     {
         // Drain the queued peer set: issue concurrent FINDNODE requests.
         size_t queries_issued = 0U;

         while (queries_issued < config_.max_concurrent_queries)
         {
             auto next = crawler_.dequeue_next();
             if (!next.has_value())
             {
                 break;
             }

             const ValidatedPeer peer = next.value();

             asio::spawn(io_context_,
                 [this, peer](asio::yield_context inner_yield)
                 {
                     auto result = send_findnode(peer, inner_yield);
                     if (!result)
                     {
                         ++send_findnode_failures_;
                         crawler_.mark_failed(peer.node_id);
                         logger_->debug("discv5 FINDNODE failed for {}:{}",
                                        peer.ip, peer.udp_port);
                     }
                     else
                     {
                         crawler_.mark_measured(peer.node_id);
                     }
                 });

             ++queries_issued;
         }

         if (queries_issued > 0U)
         {
             logger_->debug("discv5 crawler: {} FINDNODE queries issued", queries_issued);
         }

         // Sleep until next round.
         boost::system::error_code ec;
         timer.expires_after(interval);
         timer.async_wait(asio::redirect_error(yield, ec));

         if (ec && running_.load())
         {
             logger_->warn("discv5 crawler timer error: {}", ec.message());
         }
     }
 }

 // ---------------------------------------------------------------------------
 // handle_packet
 // ---------------------------------------------------------------------------

 void discv5_client::handle_packet(
     const uint8_t*       data,
     size_t               length,
     const udp::endpoint& sender) noexcept
 {
     logger_->debug("discv5: packet ({} bytes) from {}:{}",
                    length,
                    sender.address().to_string(),
                    sender.port());

     const NodeAddress local_node_addr = derive_node_address(config_.public_key);

     auto packet_result = decode_packet(data, length, local_node_addr);
     if (!packet_result)
     {
         logger_->debug("discv5: failed to decode packet from {}:{}",
                        sender.address().to_string(),
                        sender.port());
         return;
     }

     const PacketView& packet = packet_result.value();
     const std::string key = endpoint_key(sender);

     if (packet.flag == kFlagWhoareyou)
     {
         if (packet.auth_size != kWhoareyouAuthDataBytes)
         {
             return;
         }

         auto pending_it = pending_requests_.find(key);
         if (pending_it == pending_requests_.end() || pending_it->second.request_nonce != packet.nonce)
         {
             return;
         }

         std::copy_n(packet.auth_data.begin(), pending_it->second.id_nonce.size(), pending_it->second.id_nonce.begin());
         pending_it->second.record_seq = read_u64_be(packet.auth_data.data() + kWhoareyouIdNonceBytes);
         pending_it->second.challenge_data = packet.header_data;
         pending_it->second.have_challenge = true;

         ++whoareyou_packets_;
         asio::spawn(io_context_,
             [this, peer = pending_it->second.peer](asio::yield_context yield)
             {
                 auto result = send_findnode(peer, yield);
                 if (!result)
                 {
                     ++send_findnode_failures_;
                     crawler_.mark_failed(peer.node_id);
                 }
             });
         return;
     }

     if (packet.flag == kFlagMessage)
     {
         ++inbound_message_seen_;

         if (packet.auth_size != kMessageAuthDataBytes)
         {
             return;
         }

         NodeAddress remote_node_addr{};
         std::copy_n(packet.auth_data.begin(), remote_node_addr.size(), remote_node_addr.begin());

         auto session_it = sessions_.find(key);
         if (session_it == sessions_.end() || session_it->second.remote_node_addr != remote_node_addr)
         {
             asio::spawn(io_context_,
                 [this, sender, remote_node_addr, nonce = packet.nonce](asio::yield_context yield)
                 {
                     (void)send_whoareyou(sender, remote_node_addr, nonce, yield);
                 });
             return;
         }

         auto plaintext_result = decrypt_gcm(
             session_it->second.read_key,
             packet.nonce,
             packet.msg_data,
             packet.header_data);
         if (!plaintext_result)
         {
             ++inbound_message_decrypt_fail_;

             asio::spawn(io_context_,
                 [this, sender, remote_node_addr, nonce = packet.nonce](asio::yield_context yield)
                 {
                     (void)send_whoareyou(sender, remote_node_addr, nonce, yield);
                 });
             return;
         }

         const std::vector<uint8_t>& plaintext = plaintext_result.value();
         if (plaintext.empty())
         {
             return;
         }

         const uint8_t msg_type = plaintext.front();
         const std::vector<uint8_t> body(plaintext.begin() + kMessageTypePrefixBytes, plaintext.end());

         if (msg_type == kMsgNodes)
         {
             auto nodes_result = parse_nodes_message(body);
             if (!nodes_result)
             {
                 return;
             }

             if (!session_it->second.last_req_id.empty() &&
                 nodes_result.value().first != session_it->second.last_req_id)
             {
                 return;
             }

             ++nodes_packets_;
             crawler_.mark_measured(session_it->second.remote_node_id);
             crawler_.ingest_discovered_peers(nodes_result.value().second);
             return;
         }

         if (msg_type == kMsgFindNode)
         {
             auto req_id_result = parse_findnode_req_id(body);
             if (!req_id_result)
             {
                 return;
             }

             asio::spawn(io_context_,
                 [this, sender, req_id = req_id_result.value()](asio::yield_context yield)
                 {
                     (void)handle_findnode_request(req_id, sender, yield);
                 });
         }

         return;
     }

     if (packet.flag == kFlagHandshake)
     {
          ++inbound_handshake_seen_;

         auto auth_result = parse_handshake_auth(packet.auth_data);
         if (!auth_result)
         {
              ++inbound_handshake_reject_auth_;
             return;
         }

         auto challenge_it = sent_challenges_.find(key);
         if (challenge_it == sent_challenges_.end() || auth_result.value().src_id != challenge_it->second.remote_node_addr)
         {
              ++inbound_handshake_reject_challenge_;
             return;
         }

         if (auth_result.value().record.empty())
         {
              ++inbound_handshake_reject_record_;
             return;
         }

         auto record_result = EnrParser::decode_rlp(auth_result.value().record);
         if (!record_result)
         {
              ++inbound_handshake_reject_record_;
             return;
         }

         auto verify_result = EnrParser::verify_signature(record_result.value());
         if (!verify_result)
         {
              ++inbound_handshake_reject_record_;
             return;
         }

         const NodeAddress record_node_addr = derive_node_address(record_result.value().node_id);
         if (record_node_addr != auth_result.value().src_id)
         {
              ++inbound_handshake_reject_record_;
             return;
         }

         const NodeAddress local_id = derive_node_address(config_.public_key);
         if (!verify_id_signature(
                 record_result.value().node_id,
                 auth_result.value().signature,
                 challenge_it->second.challenge_data,
                 auth_result.value().pubkey,
                 local_id))
         {
              ++inbound_handshake_reject_record_;
             return;
         }

         auto shared_result = shared_secret_from_compressed_pubkey(auth_result.value().pubkey, config_.private_key);
         if (!shared_result)
         {
              ++inbound_handshake_reject_crypto_;
             return;
         }

         auto keys_result = derive_session_keys(
             shared_result.value(),
             challenge_it->second.challenge_data,
             auth_result.value().src_id,
             local_id);
         if (!keys_result)
         {
              ++inbound_handshake_reject_crypto_;
             return;
         }

         SessionState session;
         session.write_key = keys_result.value().second;
         session.read_key = keys_result.value().first;
         session.remote_node_addr = auth_result.value().src_id;
         session.remote_node_id = record_result.value().node_id;
         sessions_[key] = session;

         auto plaintext_result = decrypt_gcm(
             sessions_[key].read_key,
             packet.nonce,
             packet.msg_data,
             packet.header_data);
         if (!plaintext_result)
         {
             sessions_.erase(key);
              ++inbound_handshake_reject_decrypt_;
             return;
         }

         ++handshake_packets_;
         sent_challenges_.erase(key);

         const std::vector<uint8_t>& plaintext = plaintext_result.value();
         if (plaintext.empty())
         {
             return;
         }

         if (plaintext.front() == kMsgFindNode)
         {
             auto req_id_result = parse_findnode_req_id(
                 std::vector<uint8_t>(plaintext.begin() + kMessageTypePrefixBytes, plaintext.end()));
             if (!req_id_result)
             {
                 return;
             }

             sessions_[key].last_req_id = req_id_result.value();
             asio::spawn(io_context_,
                 [this, sender, req_id = req_id_result.value()](asio::yield_context yield)
                 {
                     (void)handle_findnode_request(req_id, sender, yield);
                 });
         }
     }
 }

 VoidResult discv5_client::send_findnode(const ValidatedPeer& peer, asio::yield_context yield)
 {
     const std::string key = endpoint_key(peer.ip, peer.udp_port);
     const NodeAddress local_node_addr = derive_node_address(config_.public_key);
     const NodeAddress remote_node_addr = derive_node_address(peer.node_id);

     std::vector<uint8_t> req_id =
     {
         static_cast<uint8_t>((peer.udp_port >> 8U) & 0xFFU),
         static_cast<uint8_t>(peer.udp_port & 0xFFU),
         static_cast<uint8_t>((peer.tcp_port >> 8U) & 0xFFU),
         static_cast<uint8_t>(peer.tcp_port & 0xFFU),
     };

     auto session_it = sessions_.find(key);
     if (session_it != sessions_.end())
     {
         auto plaintext_result = make_findnode_plaintext(req_id);
         if (!plaintext_result)
         {
             return plaintext_result.error();
         }

         std::array<uint8_t, kGcmNonceBytes> nonce{};
         if (!random_bytes(nonce.data(), nonce.size()))
         {
             return discv5Error::kNetworkSendFailed;
         }

         const std::vector<uint8_t> auth_data = make_message_auth_data(local_node_addr);
         std::vector<uint8_t> header_data;
         auto packet_result = encode_packet(
             kFlagMessage,
             nonce,
             auth_data,
             {},
             remote_node_addr,
             &header_data);
         if (!packet_result)
         {
             return packet_result.error();
         }

         auto encrypted_msg_result = encrypt_gcm(
             session_it->second.write_key,
             nonce,
             plaintext_result.value(),
             header_data);
         if (!encrypted_msg_result)
         {
             return encrypted_msg_result.error();
         }

          std::vector<uint8_t> packet = std::move(packet_result.value());
          packet.insert(
              packet.end(),
              encrypted_msg_result.value().begin(),
              encrypted_msg_result.value().end());

         session_it->second.last_req_id = req_id;
          return send_packet(packet, peer, yield);
     }

     auto pending_it = pending_requests_.find(key);
     if (pending_it != pending_requests_.end() && !pending_it->second.have_challenge)
     {
         pending_requests_.erase(pending_it);
         pending_it = pending_requests_.end();
     }

     if (pending_it == pending_requests_.end())
     {
         PendingRequest pending;
         pending.peer = peer;
         pending.req_id = req_id;
         if (!random_bytes(pending.request_nonce.data(), pending.request_nonce.size()))
         {
             return discv5Error::kNetworkSendFailed;
         }
         pending_requests_[key] = pending;

         const std::vector<uint8_t> auth_data = make_message_auth_data(local_node_addr);
         std::vector<uint8_t> random_msg(kRandomMessageCiphertextBytes);
         if (!random_bytes(random_msg.data(), random_msg.size()))
         {
             return discv5Error::kNetworkSendFailed;
         }

         auto packet_result = encode_packet(
             kFlagMessage,
             pending.request_nonce,
             auth_data,
             random_msg,
             remote_node_addr);
         if (!packet_result)
         {
             pending_requests_.erase(key);
             return packet_result.error();
         }

         return send_packet(packet_result.value(), peer, yield);
     }


      ++outbound_handshake_attempts_;

     auto eph_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
     if (!eph_result)
     {
          ++outbound_handshake_failures_;
         return discv5Error::kContextCreationFailed;
     }

     auto eph_compressed_result = compress_public_key(eph_result.value().public_key);
     if (!eph_compressed_result)
     {
          ++outbound_handshake_failures_;
         return eph_compressed_result.error();
     }

     auto signature_result = make_id_signature(
         config_.private_key,
         pending_it->second.challenge_data,
         std::vector<uint8_t>(eph_compressed_result.value().begin(), eph_compressed_result.value().end()),
         remote_node_addr);
     if (!signature_result)
     {
          ++outbound_handshake_failures_;
         return signature_result.error();
     }

      std::vector<uint8_t> local_enr_record;
      if (pending_it->second.record_seq < static_cast<uint64_t>(kInitialEnrSeq))
      {
          auto enr_result = build_local_enr();
          if (!enr_result)
          {
              ++outbound_handshake_failures_;
              return enr_result.error();
          }
          local_enr_record = std::move(enr_result.value());
      }

     auto shared_result = shared_secret_from_uncompressed_pubkey(
         pending_it->second.peer.node_id,
         eph_result.value().private_key);
     if (!shared_result)
     {
          ++outbound_handshake_failures_;
         return shared_result.error();
     }

     auto keys_result = derive_session_keys(
         shared_result.value(),
         pending_it->second.challenge_data,
         local_node_addr,
         remote_node_addr);
     if (!keys_result)
     {
          ++outbound_handshake_failures_;
         return keys_result.error();
     }

     std::vector<uint8_t> auth_data;
      auth_data.reserve(kHandshakeAuthFixedBytes + signature_result.value().size() + eph_compressed_result.value().size() + local_enr_record.size());
     auth_data.insert(auth_data.end(), local_node_addr.begin(), local_node_addr.end());
     auth_data.push_back(static_cast<uint8_t>(signature_result.value().size()));
     auth_data.push_back(static_cast<uint8_t>(eph_compressed_result.value().size()));
     auth_data.insert(auth_data.end(), signature_result.value().begin(), signature_result.value().end());
     auth_data.insert(auth_data.end(), eph_compressed_result.value().begin(), eph_compressed_result.value().end());
      auth_data.insert(auth_data.end(), local_enr_record.begin(), local_enr_record.end());

     auto plaintext_result = make_findnode_plaintext(pending_it->second.req_id);
     if (!plaintext_result)
     {
          ++outbound_handshake_failures_;
         return plaintext_result.error();
     }

     std::array<uint8_t, kGcmNonceBytes> nonce{};
     if (!random_bytes(nonce.data(), nonce.size()))
     {
          ++outbound_handshake_failures_;
         return discv5Error::kNetworkSendFailed;
     }

     std::vector<uint8_t> header_data;
     auto packet_placeholder_result = encode_packet(
         kFlagHandshake,
         nonce,
         auth_data,
         {},
         remote_node_addr,
         &header_data);
     if (!packet_placeholder_result)
     {
          ++outbound_handshake_failures_;
         return packet_placeholder_result.error();
     }

     auto encrypted_result = encrypt_gcm(
         keys_result.value().first,
         nonce,
         plaintext_result.value(),
         header_data);
     if (!encrypted_result)
     {
          ++outbound_handshake_failures_;
         return encrypted_result.error();
     }

      std::vector<uint8_t> handshake_packet = std::move(packet_placeholder_result.value());
      handshake_packet.insert(
          handshake_packet.end(),
          encrypted_result.value().begin(),
          encrypted_result.value().end());

     SessionState session;
     session.write_key = keys_result.value().first;
     session.read_key = keys_result.value().second;
     session.remote_node_addr = remote_node_addr;
     session.remote_node_id = pending_it->second.peer.node_id;
     session.last_req_id = pending_it->second.req_id;
     sessions_[key] = session;
     pending_requests_.erase(key);

        auto send_result = send_packet(handshake_packet, peer, yield);
      if (!send_result)
      {
          ++outbound_handshake_failures_;
          return send_result.error();
      }

      return send_result;
 }

 VoidResult discv5_client::send_packet(
     const std::vector<uint8_t>& packet,
     const ValidatedPeer& peer,
     asio::yield_context yield)
 {
     boost::system::error_code ec;
     const auto address = asio::ip::make_address(peer.ip, ec);
     if (ec)
     {
         logger_->warn("discv5 send address parse failed for {}:{}: {}",
                       peer.ip, peer.udp_port, ec.message());
         return discv5Error::kNetworkSendFailed;
     }

     const udp::endpoint destination(address, peer.udp_port);
     socket_.async_send_to(
         asio::buffer(packet),
         destination,
         asio::redirect_error(yield, ec));

     if (ec)
     {
         logger_->warn("discv5 UDP send to {}:{} failed: {}",
                       peer.ip, peer.udp_port, ec.message());
         return discv5Error::kNetworkSendFailed;
     }

     return rlp::outcome::success();
 }

 VoidResult discv5_client::send_whoareyou(
     const udp::endpoint& sender,
     const std::array<uint8_t, kKeccak256Bytes>& remote_node_addr,
     const std::array<uint8_t, kGcmNonceBytes>& request_nonce,
     asio::yield_context yield)
 {
     ChallengeState challenge;
     challenge.remote_node_addr = remote_node_addr;
     challenge.request_nonce = request_nonce;
     challenge.record_seq = 0U;
     if (!random_bytes(challenge.id_nonce.data(), challenge.id_nonce.size()))
     {
         return discv5Error::kNetworkSendFailed;
     }

     std::vector<uint8_t> auth_data;
     auth_data.reserve(kWhoareyouAuthDataBytes);
     auth_data.insert(auth_data.end(), challenge.id_nonce.begin(), challenge.id_nonce.end());
     append_u64_be(auth_data, challenge.record_seq);

     auto packet_result = encode_packet(
         kFlagWhoareyou,
         request_nonce,
         auth_data,
         {},
         remote_node_addr,
         &challenge.challenge_data);
     if (!packet_result)
     {
         return packet_result.error();
     }

     sent_challenges_[endpoint_key(sender)] = challenge;

     boost::system::error_code ec;
     socket_.async_send_to(
         asio::buffer(packet_result.value()),
         sender,
         asio::redirect_error(yield, ec));
     if (ec)
     {
         return discv5Error::kNetworkSendFailed;
     }

     return rlp::outcome::success();
 }

 VoidResult discv5_client::handle_findnode_request(
     const std::vector<uint8_t>& req_id,
     const udp::endpoint& sender,
     asio::yield_context yield)
 {
     const std::string key = endpoint_key(sender);
     auto session_it = sessions_.find(key);
     if (session_it == sessions_.end())
     {
         return discv5Error::kNetworkSendFailed;
     }

     auto enr_result = build_local_enr();
     if (!enr_result)
     {
         return enr_result.error();
     }

     auto plaintext_result = make_nodes_plaintext(req_id, enr_result.value());
     if (!plaintext_result)
     {
         return plaintext_result.error();
     }

     std::array<uint8_t, kGcmNonceBytes> nonce{};
     if (!random_bytes(nonce.data(), nonce.size()))
     {
         return discv5Error::kNetworkSendFailed;
     }

     const NodeAddress local_node_addr = derive_node_address(config_.public_key);
     const std::vector<uint8_t> auth_data = make_message_auth_data(local_node_addr);

     std::vector<uint8_t> header_data;
     auto header_result = encode_packet(
         kFlagMessage,
         nonce,
         auth_data,
         {},
         session_it->second.remote_node_addr,
         &header_data);
     if (!header_result)
     {
         return header_result.error();
     }

     auto encrypted_result = encrypt_gcm(
         session_it->second.write_key,
         nonce,
         plaintext_result.value(),
         header_data);
     if (!encrypted_result)
     {
         return encrypted_result.error();
     }

      std::vector<uint8_t> packet = std::move(header_result.value());
      packet.insert(
          packet.end(),
          encrypted_result.value().begin(),
          encrypted_result.value().end());

     ValidatedPeer peer;
     peer.node_id = session_it->second.remote_node_id;
     peer.ip = sender.address().to_string();
     peer.udp_port = sender.port();
     peer.tcp_port = sender.port();
      return send_packet(packet, peer, yield);
 }

 Result<std::vector<uint8_t>> discv5_client::build_local_enr() noexcept
 {
     boost::system::error_code ec;
     const auto endpoint = socket_.local_endpoint(ec);
     if (ec)
     {
         return discv5Error::kNetworkSendFailed;
     }

     return make_local_enr_record(config_, endpoint.port());
 }

 } // namespace discv5
