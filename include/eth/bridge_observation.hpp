// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_BRIDGE_OBSERVATION_HPP
#define EVMRELAY_INCLUDE_ETH_BRIDGE_OBSERVATION_HPP

#include <eth/bridge_event.hpp>
#include <eth/secp256k1_utility.hpp>
#include <optional>

namespace eth {

/// @brief Canonical bytes for bridge-event consensus payloads and watcher signatures.
[[nodiscard]] codec::ByteBuffer bridge_event_claim_payload(const BridgeEventClaim& claim);
[[nodiscard]] std::optional<BridgeEventClaim> decode_bridge_event_claim_payload(
    const codec::ByteBuffer& payload) noexcept;

[[nodiscard]] Hash256 bridge_event_domain_separator(
    uint64_t       src_chain_id,
    uint64_t       dest_chain_id,
    const Address& bridge_contract) noexcept;

[[nodiscard]] Hash256 bridge_event_claim_hash(const BridgeEventClaim& claim) noexcept;
[[nodiscard]] std::optional<Address> observer_address_from_private_key(
    const Secp256k1PrivateKey& private_key) noexcept;
[[nodiscard]] std::optional<BridgeEventObservation> sign_bridge_event_claim(
    const BridgeEventClaim&     claim,
    const Secp256k1PrivateKey&  private_key) noexcept;
[[nodiscard]] bool verify_bridge_event_observation(
    const BridgeEventObservation& observation) noexcept;

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_BRIDGE_OBSERVATION_HPP
