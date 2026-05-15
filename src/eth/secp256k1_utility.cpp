// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/secp256k1_utility.hpp>
#include <eth/abi_decoder.hpp>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <algorithm>

namespace eth {

namespace {

constexpr size_t kCompactSignatureBytes = 64;
constexpr size_t kRecoverableSignatureBytes = 65;
constexpr size_t kUncompressedPublicKeyBytes = 65;
constexpr uint8_t kUncompressedPublicKeyPrefix = 0x04;

Address address_from_uncompressed_public_key(
    const std::array<uint8_t, kUncompressedPublicKeyBytes>& public_key)
{
    codec::ByteBuffer payload(public_key.begin() + 1, public_key.end());
    const auto hash = abi::keccak256(payload.data(), payload.size());

    Address address{};
    std::copy(hash.end() - address.size(), hash.end(), address.begin());
    return address;
}

std::optional<Address> address_from_public_key(secp256k1_context* context, const secp256k1_pubkey& public_key)
{
    std::array<uint8_t, kUncompressedPublicKeyBytes> public_key_bytes{};
    size_t public_key_length = public_key_bytes.size();
    if (!secp256k1_ec_pubkey_serialize(
            context,
            public_key_bytes.data(),
            &public_key_length,
            &public_key,
            SECP256K1_EC_UNCOMPRESSED))
    {
        return std::nullopt;
    }

    if (public_key_length != public_key_bytes.size()
        || public_key_bytes.front() != kUncompressedPublicKeyPrefix)
    {
        return std::nullopt;
    }

    return address_from_uncompressed_public_key(public_key_bytes);
}

} // namespace

std::optional<Address> secp256k1_address_from_private_key(
    const Secp256k1PrivateKey& private_key) noexcept
{
    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    if (!secp256k1_ec_seckey_verify(context, private_key.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_pubkey public_key{};
    if (!secp256k1_ec_pubkey_create(context, &public_key, private_key.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    const auto address = address_from_public_key(context, public_key);
    secp256k1_context_destroy(context);
    return address;
}

std::optional<Address> secp256k1_recover_address(
    const Hash256&           message_hash,
    const codec::ByteBuffer& recoverable_signature) noexcept
{
    if (recoverable_signature.size() != kRecoverableSignatureBytes)
    {
        return std::nullopt;
    }

    const int recovery_id = static_cast<int>(recoverable_signature[kCompactSignatureBytes]);
    if (recovery_id < 0 || recovery_id > 3)
    {
        return std::nullopt;
    }

    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    secp256k1_ecdsa_recoverable_signature signature{};
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(
            context,
            &signature,
            recoverable_signature.data(),
            recovery_id))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_pubkey public_key{};
    if (!secp256k1_ecdsa_recover(context, &public_key, &signature, message_hash.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    const auto address = address_from_public_key(context, public_key);
    secp256k1_context_destroy(context);
    return address;
}

std::optional<codec::ByteBuffer> secp256k1_sign_recoverable(
    const Hash256&             message_hash,
    const Secp256k1PrivateKey& private_key) noexcept
{
    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    if (!secp256k1_ec_seckey_verify(context, private_key.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_ecdsa_recoverable_signature signature{};
    if (!secp256k1_ecdsa_sign_recoverable(
            context,
            &signature,
            message_hash.data(),
            private_key.data(),
            nullptr,
            nullptr))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    codec::ByteBuffer signature_bytes(kRecoverableSignatureBytes);
    int recovery_id = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(
        context,
        signature_bytes.data(),
        &recovery_id,
        &signature);
    secp256k1_context_destroy(context);

    signature_bytes[kCompactSignatureBytes] = static_cast<uint8_t>(recovery_id);
    return signature_bytes;
}

} // namespace eth
