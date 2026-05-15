// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/abi_decoder.hpp>
#include <eth/eth_constants.hpp>
#include <rlp/errors.hpp>
#include <algorithm>

#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>

namespace eth::abi {

namespace {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Read a big-endian uint256 from exactly 32 bytes into intx::uint256.
/// uint<256>::words_[0] = least significant 64 bits, words_[3] = most significant.
intx::uint256 read_uint256_be(const uint8_t* data) noexcept
{
    auto read64be = [](const uint8_t* p) -> uint64_t
    {
        return (static_cast<uint64_t>(p[0]) << 56)
             | (static_cast<uint64_t>(p[1]) << 48)
             | (static_cast<uint64_t>(p[2]) << 40)
             | (static_cast<uint64_t>(p[3]) << 32)
             | (static_cast<uint64_t>(p[4]) << 24)
             | (static_cast<uint64_t>(p[5]) << 16)
             | (static_cast<uint64_t>(p[6]) <<  8)
             |  static_cast<uint64_t>(p[7]);
    };

    // Variadic ctor fills words_[] in order: words_[0], words_[1], words_[2], words_[3]
    // words_[0] = LS 64 bits = data[24..31]
    // words_[3] = MS 64 bits = data[0..7]
    return intx::uint256{
        read64be(data + 24),   // words_[0] — least significant
        read64be(data + 16),   // words_[1]
        read64be(data +  8),   // words_[2]
        read64be(data +  0)    // words_[3] — most significant
    };
}

/// True if the ABI type is dynamic (uses offset+tail encoding).
bool is_dynamic(AbiParamKind kind) noexcept
{
    return kind == AbiParamKind::kBytes || kind == AbiParamKind::kString;
}

} // namespace

// ---------------------------------------------------------------------------
// keccak256
// ---------------------------------------------------------------------------

codec::Hash256 keccak256(const uint8_t* data, size_t len) noexcept
{
    auto hash = nil::crypto3::hash<nil::crypto3::hashes::keccak_1600<256>>(data, data + len);
    std::array<uint8_t, kKeccak256Size> hash_array = hash;
    codec::Hash256 out{};
    std::copy(hash_array.begin(), hash_array.end(), out.begin());
    return out;
}

codec::Hash256 keccak256(const std::string& text) noexcept
{
    return keccak256(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

codec::Hash256 event_signature_hash(const std::string& signature) noexcept
{
    return keccak256(signature);
}

// ---------------------------------------------------------------------------
// decode_abi_word
// ---------------------------------------------------------------------------

rlp::Result<AbiValue> decode_abi_word(
    const codec::Hash256& word,
    AbiParamKind          kind) noexcept
{
    switch (kind)
    {
        case AbiParamKind::kAddress:
        {
            // Address occupies the rightmost kAbiAddressSize bytes of the 32-byte word.
            codec::Address addr{};
            std::copy(word.begin() + kAbiAddressPadding, word.end(), addr.begin());
            return AbiValue{addr};
        }

        case AbiParamKind::kUint:
        case AbiParamKind::kInt:
        {
            return AbiValue{read_uint256_be(word.data())};
        }

        case AbiParamKind::kBytes32:
        {
            return AbiValue{word};
        }

        case AbiParamKind::kBool:
        {
            // Any non-zero byte in the word means true; canonically it's word[kAbiBoolByteIndex].
            bool val = (word[kAbiBoolByteIndex] != 0);
            return AbiValue{val};
        }

        case AbiParamKind::kBytes:
        case AbiParamKind::kString:
        {
            // Dynamic types are hashed when indexed — value cannot be recovered.
            // Return empty placeholder.
            if (kind == AbiParamKind::kBytes)
            {
                return AbiValue{codec::ByteBuffer{}};
            }
            return AbiValue{std::string{}};
        }
    }

    return rlp::DecodingError::kUnexpectedString;
}

// ---------------------------------------------------------------------------
// decode_indexed_param
// ---------------------------------------------------------------------------

rlp::Result<AbiValue> decode_indexed_param(
    const codec::Hash256& topic,
    const AbiParam&       param) noexcept
{
    return decode_abi_word(topic, param.kind);
}

// ---------------------------------------------------------------------------
// decode_log_data
// ---------------------------------------------------------------------------

rlp::Result<std::vector<AbiValue>> decode_log_data(
    const codec::ByteBuffer&     data,
    const std::vector<AbiParam>& params) noexcept
{
    if (params.empty())
    {
        return std::vector<AbiValue>{};
    }

    // Each head slot is kAbiWordSize bytes.  Dynamic types store an offset in the head.
    const size_t head_size = params.size() * kAbiWordSize;

    if (data.size() < head_size)
    {
        return rlp::DecodingError::kInputTooShort;
    }

    std::vector<AbiValue> results;
    results.reserve(params.size());

    for (size_t i = 0; i < params.size(); ++i)
    {
        const AbiParamKind kind = params[i].kind;
        const size_t       head_offset = i * kAbiWordSize;

        if (!is_dynamic(kind))
        {
            codec::Hash256 word{};
            std::copy(
                data.data() + head_offset,
                data.data() + head_offset + kAbiWordSize,
                word.begin());

            auto val = decode_abi_word(word, kind);
            if (!val) { return val.error(); }
            results.push_back(std::move(val.value()));
        }
        else
        {
            // Head slot holds the byte offset into the data buffer where the
            // actual value starts.
            codec::Hash256 offset_word{};
            std::copy(
                data.data() + head_offset,
                data.data() + head_offset + kAbiWordSize,
                offset_word.begin());

            const intx::uint256 raw_offset = read_uint256_be(offset_word.data());

            // Offset must fit in size_t and point inside the buffer.
            if (raw_offset > std::numeric_limits<size_t>::max())
            {
                return rlp::DecodingError::kOverflow;
            }

            const size_t tail_offset = static_cast<size_t>(raw_offset);

            // Read kAbiWordSize-byte length prefix at tail_offset.
            if (data.size() < tail_offset + kAbiWordSize)
            {
                return rlp::DecodingError::kInputTooShort;
            }

            codec::Hash256 len_word{};
            std::copy(
                data.data() + tail_offset,
                data.data() + tail_offset + kAbiWordSize,
                len_word.begin());

            const intx::uint256 raw_len = read_uint256_be(len_word.data());

            if (raw_len > std::numeric_limits<size_t>::max())
            {
                return rlp::DecodingError::kOverflow;
            }

            const size_t content_len    = static_cast<size_t>(raw_len);
            const size_t content_offset = tail_offset + kAbiWordSize;

            if (data.size() < content_offset + content_len)
            {
                return rlp::DecodingError::kInputTooShort;
            }

            if (kind == AbiParamKind::kBytes)
            {
                codec::ByteBuffer buf(
                    data.data() + content_offset,
                    data.data() + content_offset + content_len);
                results.push_back(AbiValue{std::move(buf)});
            }
            else // kString
            {
                std::string str(
                    reinterpret_cast<const char*>(data.data() + content_offset),
                    content_len);
                results.push_back(AbiValue{std::move(str)});
            }
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// decode_log
// ---------------------------------------------------------------------------

rlp::Result<std::vector<AbiValue>> decode_log(
    const codec::LogEntry&       log,
    const std::string&           signature,
    const std::vector<AbiParam>& params) noexcept
{
    // Verify topic[0] matches the signature hash.
    if (log.topics.empty())
    {
        return rlp::DecodingError::kInputTooShort;
    }

    const codec::Hash256 expected_sig = event_signature_hash(signature);
    if (log.topics[0] != expected_sig)
    {
        return rlp::DecodingError::kUnexpectedString;
    }

    std::vector<AbiValue> results;
    results.reserve(params.size());

    size_t topic_index = 1; // topics[0] is the signature hash
    std::vector<AbiParam> data_params;

    for (const auto& param : params)
    {
        if (param.indexed)
        {
            if (topic_index >= log.topics.size())
            {
                return rlp::DecodingError::kInputTooShort;
            }

            auto val = decode_indexed_param(log.topics[topic_index], param);
            if (!val) { return val.error(); }
            results.push_back(std::move(val.value()));
            ++topic_index;
        }
        else
        {
            data_params.push_back(param);
        }
    }

    // Decode non-indexed params from data field.
    auto data_vals = decode_log_data(log.data, data_params);
    if (!data_vals) { return data_vals.error(); }

    for (auto& v : data_vals.value())
    {
        results.push_back(std::move(v));
    }

    return results;
}

} // namespace eth::abi

