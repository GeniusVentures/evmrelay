// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/bridge_observation.hpp>
#include <eth/abi_decoder.hpp>
#include <base/byte_encoding.hpp>
#include <algorithm>
#include <string_view>

namespace eth {

namespace {

constexpr std::string_view kBridgeEventDomain = "GNUS_BRIDGE_EVENT_V1";

class PayloadReader
{
public:
    explicit PayloadReader(const codec::ByteBuffer& payload) noexcept
        : data_(payload.data()), size_(payload.size())
    {
    }

    [[nodiscard]] bool read_domain() noexcept
    {
        if (remaining() < kBridgeEventDomain.size())
        {
            return false;
        }
        const std::string_view actual(
            reinterpret_cast<const char*>(data_ + offset_),
            kBridgeEventDomain.size());
        if (actual != kBridgeEventDomain)
        {
            return false;
        }
        offset_ += kBridgeEventDomain.size();
        return true;
    }

    [[nodiscard]] bool read_u32(uint32_t& value) noexcept
    {
        if (remaining() < sizeof(uint32_t))
        {
            return false;
        }
        value = 0;
        for (size_t i = 0; i < sizeof(uint32_t); ++i)
        {
            value = (value << 8) | data_[offset_++];
        }
        return true;
    }

    [[nodiscard]] bool read_u64(uint64_t& value) noexcept
    {
        if (remaining() < sizeof(uint64_t))
        {
            return false;
        }
        value = 0;
        for (size_t i = 0; i < sizeof(uint64_t); ++i)
        {
            value = (value << 8) | data_[offset_++];
        }
        return true;
    }

    template <size_t N>
    [[nodiscard]] bool read_array(std::array<uint8_t, N>& value) noexcept
    {
        if (remaining() < N)
        {
            return false;
        }
        std::copy(data_ + offset_, data_ + offset_ + N, value.begin());
        offset_ += N;
        return true;
    }

    [[nodiscard]] bool read_uint256(intx::uint256& value) noexcept
    {
        if (remaining() < 32)
        {
            return false;
        }
        value = 0;
        for (size_t i = 0; i < 32; ++i)
        {
            value = (value << 8) | data_[offset_++];
        }
        return true;
    }

    template <size_t N>
    [[nodiscard]] bool read_length_prefixed_arrays(
        std::vector<std::array<uint8_t, N>>& values) noexcept
    {
        uint64_t count = 0;
        if (!read_u64(count) || count > remaining() / N)
        {
            return false;
        }
        values.clear();
        values.reserve(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i)
        {
            std::array<uint8_t, N> value{};
            if (!read_array(value))
            {
                return false;
            }
            values.push_back(value);
        }
        return true;
    }

    [[nodiscard]] bool read_length_prefixed_bytes(codec::ByteBuffer& value) noexcept
    {
        uint64_t length = 0;
        if (!read_u64(length) || length > remaining())
        {
            return false;
        }
        value.assign(data_ + offset_, data_ + offset_ + length);
        offset_ += static_cast<size_t>(length);
        return true;
    }

    [[nodiscard]] bool finished() const noexcept
    {
        return offset_ == size_;
    }

private:
    [[nodiscard]] size_t remaining() const noexcept
    {
        return size_ - offset_;
    }

    const uint8_t* data_ = nullptr;
    size_t         size_ = 0;
    size_t         offset_ = 0;
};

} // namespace

codec::ByteBuffer bridge_event_claim_payload(const BridgeEventClaim& claim)
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

std::optional<BridgeEventClaim> decode_bridge_event_claim_payload(
    const codec::ByteBuffer& payload) noexcept
{
    PayloadReader reader(payload);
    BridgeEventClaim claim;

    if (!reader.read_domain() ||
        !reader.read_u64(claim.src_chain_id) ||
        !reader.read_u64(claim.dest_chain_id) ||
        !reader.read_array(claim.bridge_contract) ||
        !reader.read_u64(claim.block_number) ||
        !reader.read_array(claim.block_hash) ||
        !reader.read_array(claim.tx_hash) ||
        !reader.read_u32(claim.log_index) ||
        !reader.read_array(claim.event_topic0) ||
        !reader.read_length_prefixed_arrays(claim.topics) ||
        !reader.read_length_prefixed_bytes(claim.data) ||
        !reader.read_array(claim.sender) ||
        !reader.read_uint256(claim.token_id_or_nonce) ||
        !reader.read_uint256(claim.amount) ||
        !reader.read_array(claim.recipient) ||
        !reader.read_u64(claim.observed_at) ||
        !reader.read_u64(claim.finality_depth) ||
        !reader.finished())
    {
        return std::nullopt;
    }

    return claim;
}

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
    const auto bytes = bridge_event_claim_payload(claim);
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
