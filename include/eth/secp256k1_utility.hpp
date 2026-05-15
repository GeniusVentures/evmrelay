// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_SECP256K1_UTILITY_HPP
#define EVMRELAY_INCLUDE_ETH_SECP256K1_UTILITY_HPP

#include <eth/eth_types.hpp>
#include <eth/objects.hpp>
#include <array>
#include <optional>

namespace eth {

using Secp256k1PrivateKey = std::array<uint8_t, 32>;

[[nodiscard]] std::optional<Address> secp256k1_address_from_private_key(
    const Secp256k1PrivateKey& private_key) noexcept;
[[nodiscard]] std::optional<Address> secp256k1_recover_address(
    const Hash256&             message_hash,
    const codec::ByteBuffer&   recoverable_signature) noexcept;
[[nodiscard]] std::optional<codec::ByteBuffer> secp256k1_sign_recoverable(
    const Hash256&             message_hash,
    const Secp256k1PrivateKey& private_key) noexcept;

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_SECP256K1_UTILITY_HPP
