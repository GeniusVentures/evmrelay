// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/bridge_observation.hpp>
#include <eth/abi_decoder.hpp>
#include <base/byte_encoding.hpp>
#include <string_view>

namespace eth {

namespace {

constexpr std::string_view kBridgeEventDomain = "GNUS_BRIDGE_EVENT_V1";

codec::ByteBuffer claim_signing_bytes(const BridgeEventClaim& claim)
{
    namespace bytes = rlp::base::byte_encoding;

    codec::ByteBuffer out;
    out.reserve(320 + claim.topics.size() * Hash256{}.size() + claim.data.size());

    out.insert(out.end(), kBridgeEventDomain.begin(), kBridgeEventDomain.end());
    bytes::append_u64_be(out, claim.src_chain_id);
    bytes::append_u64_be(out, claim.dest_chain_id);
    bytes::append_array(out, claim.bridge_contract);

    bytes::append_u64_be(out, claim.block_number);
    bytes::append_array(out, claim.block_hash);
    bytes::append_array(out, claim.tx_hash);
    bytes::append_u32_be(out, claim.log_index);

    bytes::append_array(out, claim.event_topic0);
    bytes::append_length_prefixed_arrays(out, claim.topics);
    bytes::append_length_prefixed_bytes(out, claim.data);

    bytes::append_array(out, claim.sender);
    bytes::append_uint256_be(out, claim.token_id_or_nonce);
    bytes::append_uint256_be(out, claim.amount);
    bytes::append_array(out, claim.recipient);

    bytes::append_u64_be(out, claim.observed_at);
    bytes::append_u64_be(out, claim.finality_depth);

    return out;
}

} // namespace

Hash256 bridge_event_domain_separator(
    uint64_t       src_chain_id,
    uint64_t       dest_chain_id,
    const Address& bridge_contract) noexcept
{
    namespace bytes = rlp::base::byte_encoding;

    codec::ByteBuffer out;
    out.insert(out.end(), kBridgeEventDomain.begin(), kBridgeEventDomain.end());
    bytes::append_u64_be(out, src_chain_id);
    bytes::append_u64_be(out, dest_chain_id);
    bytes::append_array(out, bridge_contract);
    return abi::keccak256(out.data(), out.size());
}

Hash256 bridge_event_claim_hash(const BridgeEventClaim& claim) noexcept
{
    const auto bytes = claim_signing_bytes(claim);
    return abi::keccak256(bytes.data(), bytes.size());
}

std::optional<Address> observer_address_from_private_key(
    const Secp256k1PrivateKey& private_key) noexcept
{
    return secp256k1_address_from_private_key(private_key);
}

std::optional<BridgeEventObservation> sign_bridge_event_claim(
    const BridgeEventClaim&    claim,
    const Secp256k1PrivateKey& private_key) noexcept
{
    const auto observer = observer_address_from_private_key(private_key);
    if (!observer.has_value())
    {
        return std::nullopt;
    }

    const auto message_hash = bridge_event_claim_hash(claim);
    auto signature_bytes = secp256k1_sign_recoverable(message_hash, private_key);
    if (!signature_bytes.has_value())
    {
        return std::nullopt;
    }

    return BridgeEventObservation{
        claim,
        *observer,
        std::move(*signature_bytes),
    };
}

bool verify_bridge_event_observation(
    const BridgeEventObservation& observation) noexcept
{
    const auto message_hash = bridge_event_claim_hash(observation.claim);
    const auto recovered = secp256k1_recover_address(message_hash, observation.signature);
    return recovered.has_value() && *recovered == observation.observer;
}

} // namespace eth
