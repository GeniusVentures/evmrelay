// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_DISCV5_ENR_TREE_HPP
#define EVMRELAY_INCLUDE_DISCV5_ENR_TREE_HPP

#include <functional>
#include <string>
#include <vector>

namespace discv5
{

/// @brief Parsed EIP-1459 ENR-tree URL.
struct EnrTreeUrl
{
    std::string public_key;
    std::string domain;
};

/// @brief DNS TXT lookup callback used by the ENR-tree resolver.
using DnsTxtLookupFn = std::function<std::vector<std::string>(const std::string& name)>;

/// @brief Resolve EIP-1459 DNS discovery trees into ENR URI seed records.
class EnrTreeResolver
{
public:
    explicit EnrTreeResolver(DnsTxtLookupFn lookup = {}) noexcept;

    [[nodiscard]] static bool is_enr_tree_url(const std::string& value) noexcept;
    [[nodiscard]] static bool parse_url(const std::string& value, EnrTreeUrl& out) noexcept;
    [[nodiscard]] static std::vector<std::string> system_txt_lookup(const std::string& name) noexcept;

    [[nodiscard]] std::vector<std::string> resolve(
        const std::vector<std::string>& urls,
        size_t                          max_records = 128U) const noexcept;

private:
    [[nodiscard]] std::vector<std::string> resolve_url(
        const EnrTreeUrl& url,
        size_t            max_records) const noexcept;
    void resolve_entry(
        const std::string& domain,
        const std::string& label,
        size_t             max_records,
        std::vector<std::string>& out) const noexcept;

    DnsTxtLookupFn lookup_;
};

} // namespace discv5

#endif // EVMRELAY_INCLUDE_DISCV5_ENR_TREE_HPP
