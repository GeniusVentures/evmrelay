// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_CONSTANTS_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>

namespace eth {

/// EVM ABI encoding constants (ABI spec: https://docs.soliditylang.org/en/latest/abi-spec.html)
static constexpr size_t  kAbiWordSize            = 32;   ///< Every ABI head/tail slot is 32 bytes
static constexpr size_t  kAbiAddressSize         = 20;   ///< Ethereum address is 20 bytes
static constexpr size_t  kAbiAddressPadding      = kAbiWordSize - kAbiAddressSize; ///< 12 bytes of left-padding
static constexpr size_t  kAbiBoolByteIndex       = kAbiWordSize - 1; ///< Canonical bool lives in rightmost byte (index 31)

/// EVM / RLP encoding thresholds
static constexpr uint8_t  kRlpListPrefixMin       = 0xC0U; ///< First byte >= 0xC0 signals an RLP list

/// EIP-2718 typed transaction envelope
static constexpr size_t  kTypedTxPrefixSize       = 1;    ///< One type-byte precedes the RLP payload

/// Default chain ID used when none is specified (Ethereum mainnet)
static constexpr uint64_t kDefaultChainId         = 1ULL;

/// Keccak-256 digest size (bytes)
static constexpr size_t  kKeccak256Size           = 32;

} // namespace eth


#endif // EVMRELAY_INCLUDE_ETH_ETH_CONSTANTS_HPP
