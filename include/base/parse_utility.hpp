// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_BASE_PARSE_UTILITY_HPP
#define EVMRELAY_INCLUDE_BASE_PARSE_UTILITY_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rlp::base::parse
{

inline constexpr size_t kHexCharsPerByte = 2;

[[nodiscard]] std::optional<uint8_t> hex_nibble(char c) noexcept;
[[nodiscard]] std::string_view trim_hex_prefix(std::string_view value) noexcept;
[[nodiscard]] std::optional<uint16_t> uint16_decimal(std::string_view value) noexcept;
[[nodiscard]] std::optional<uint64_t> uint64_decimal(std::string_view value) noexcept;
[[nodiscard]] std::optional<uint64_t> uint64_hex(std::string_view value) noexcept;
[[nodiscard]] std::string uint64_hex_quantity(uint64_t value);
[[nodiscard]] std::optional<std::vector<uint8_t>> hex_bytes(std::string_view value);
[[nodiscard]] std::string hex_bytes(const uint8_t* data, size_t size);
[[nodiscard]] std::string ascii_lower(std::string value);

template <size_t N>
[[nodiscard]] std::string hex_array_string(const std::array<uint8_t, N>& value)
{
    return hex_bytes(value.data(), value.size());
}

template <size_t N>
[[nodiscard]] bool hex_array(std::string_view value, std::array<uint8_t, N>& out) noexcept
{
    value = trim_hex_prefix(value);
    if (value.size() != N * kHexCharsPerByte)
    {
        return false;
    }

    for (size_t i = 0; i < N; ++i)
    {
        const size_t index = i * kHexCharsPerByte;
        const auto hi = hex_nibble(value[index]);
        const auto lo = hex_nibble(value[index + 1]);
        if (!hi.has_value() || !lo.has_value())
        {
            return false;
        }
        out[i] = static_cast<uint8_t>(((*hi) << 4) | *lo);
    }
    return true;
}

} // namespace rlp::base::parse

#endif // EVMRELAY_INCLUDE_BASE_PARSE_UTILITY_HPP
