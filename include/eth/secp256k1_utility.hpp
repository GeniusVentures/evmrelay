// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_SECP256K1_UTILITY_HPP
#define EVMRELAY_INCLUDE_ETH_SECP256K1_UTILITY_HPP

#include <eth/eth_types.hpp>
#include <eth/objects.hpp>
#include <array>
#include <optional>
#include <string>

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

/// @brief Decompress a 32-byte X-only secp256k1 public key to a 128-char hex destination.
///
/// Uses the Y parity from the bridge event's bool parameter to select the correct
/// compressed-key prefix (false = even Y / 0x02, true = odd Y / 0x03).  The
/// decompression is deterministic -- no parity trial is needed because the event
/// carries the ground-truth parity bit.
///
/// @param contract_x_bytes   32 bytes from the ABI-decoded bytes32 parameter
///                           (contract byte order: LSB first in hex).
/// @param destination_y_odd  Parity of Y from the event (false=even/0x02,
///                           true=odd/0x03).
/// @return 128-char hex destination string (X+Y concatenated in contract order),
///         or nullopt if X is all-zero or the X+parity combination is not on the
///         curve.
[[nodiscard]] std::optional<std::string> DecompressXOnlyPubkey(
    const std::array<uint8_t, 32>& contract_x_bytes,
    bool                           destination_y_odd) noexcept;

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_SECP256K1_UTILITY_HPP
