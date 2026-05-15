// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <base/parse_utility.hpp>

#include <charconv>

namespace rlp::base::parse
{

std::optional<uint8_t> hex_nibble(char c) noexcept
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
        return static_cast<uint8_t>(10 + (c - 'a'));
    }
    if (c >= 'A' && c <= 'F')
    {
        return static_cast<uint8_t>(10 + (c - 'A'));
    }
    return std::nullopt;
}

std::string_view trim_hex_prefix(std::string_view value) noexcept
{
    if (value.size() >= 2
        && value[0] == '0'
        && (value[1] == 'x' || value[1] == 'X'))
    {
        value.remove_prefix(2);
    }
    return value;
}

std::optional<uint16_t> uint16_decimal(std::string_view value) noexcept
{
    uint16_t out = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || ptr != value.data() + value.size())
    {
        return std::nullopt;
    }
    return out;
}

std::optional<uint64_t> uint64_decimal(std::string_view value) noexcept
{
    uint64_t out = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || ptr != value.data() + value.size())
    {
        return std::nullopt;
    }
    return out;
}

std::optional<uint64_t> uint64_hex(std::string_view value) noexcept
{
    value = trim_hex_prefix(value);
    if (value.empty())
    {
        return std::nullopt;
    }

    uint64_t out = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out, 16);
    if (ec != std::errc{} || ptr != value.data() + value.size())
    {
        return std::nullopt;
    }
    return out;
}

std::string ascii_lower(std::string value)
{
    for (char& ch : value)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

} // namespace rlp::base::parse
