// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <base/parse_utility.hpp>

#include <charconv>

namespace rlp::base::parse
{

namespace {

constexpr char kHexAlphabet[] = "0123456789abcdef";

} // namespace

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

std::string uint64_hex_quantity(uint64_t value)
{
    if (value == 0)
    {
        return "0x0";
    }

    char buffer[16]{};
    auto [ptr, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value, 16);
    if (ec != std::errc{})
    {
        return {};
    }
    return "0x" + std::string(buffer, ptr);
}

std::optional<std::vector<uint8_t>> hex_bytes(std::string_view value)
{
    value = trim_hex_prefix(value);
    if ((value.size() % 2) != 0)
    {
        return std::nullopt;
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2)
    {
        const auto hi = hex_nibble(value[i]);
        const auto lo = hex_nibble(value[i + 1]);
        if (!hi.has_value() || !lo.has_value())
        {
            return std::nullopt;
        }
        bytes.push_back(static_cast<uint8_t>((*hi << 4) | *lo));
    }
    return bytes;
}

std::string hex_bytes(const uint8_t* data, size_t size)
{
    std::string out;
    out.reserve(2 + (size * 2));
    out.append("0x");
    for (size_t i = 0; i < size; ++i)
    {
        const uint8_t byte = data[i];
        out.push_back(kHexAlphabet[byte >> 4]);
        out.push_back(kHexAlphabet[byte & 0x0f]);
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
