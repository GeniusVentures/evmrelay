// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_BASE_BYTE_ENCODING_HPP
#define EVMRELAY_INCLUDE_BASE_BYTE_ENCODING_HPP

#include <rlp/intx.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace rlp::base::byte_encoding
{

using ByteBuffer = std::vector<uint8_t>;

inline void append_u32_be(ByteBuffer& out, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

inline void append_u64_be(ByteBuffer& out, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

inline void append_uint256_be(ByteBuffer& out, intx::uint256 value)
{
    for (int byte = 31; byte >= 0; --byte)
    {
        out.push_back(static_cast<uint8_t>((value >> (byte * 8)) & 0xFF));
    }
}

template <size_t N>
inline void append_array(ByteBuffer& out, const std::array<uint8_t, N>& value)
{
    out.insert(out.end(), value.begin(), value.end());
}

inline void append_length_prefixed_bytes(ByteBuffer& out, const ByteBuffer& value)
{
    append_u64_be(out, static_cast<uint64_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

template <size_t N>
inline void append_length_prefixed_arrays(ByteBuffer& out, const std::vector<std::array<uint8_t, N>>& values)
{
    append_u64_be(out, static_cast<uint64_t>(values.size()));
    for (const auto& value : values)
    {
        append_array(out, value);
    }
}

} // namespace rlp::base::byte_encoding

#endif // EVMRELAY_INCLUDE_BASE_BYTE_ENCODING_HPP
