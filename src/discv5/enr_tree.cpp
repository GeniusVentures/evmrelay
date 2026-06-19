// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <discv5/enr_tree.hpp>
#include <discv5/discv5_enr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <sstream>
#include <string_view>
#include <unordered_set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windns.h>
#else
#include <arpa/nameser.h>
#include <resolv.h>
#endif

namespace discv5
{
namespace
{

inline constexpr std::string_view kEnrTreePrefix = "enrtree://";
inline constexpr std::string_view kRootPrefix = "enrtree-root:v1";
inline constexpr std::string_view kBranchPrefix = "enrtree-branch:";
inline constexpr std::string_view kEnrPrefix = "enr:";
inline constexpr size_t kMaxDnsResponseBytes = 65536U;
inline constexpr size_t kMaxTreeLookups = 8192U;

std::string trim_copy(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
    {
        value.remove_prefix(1U);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
    {
        value.remove_suffix(1U);
    }
    return std::string(value);
}

bool starts_with(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() && value.substr(0U, prefix.size()) == prefix;
}

std::string root_entry_label(std::string_view root)
{
    std::istringstream input{std::string(root)};
    std::string token;
    while (input >> token)
    {
        if (starts_with(token, "e="))
        {
            return token.substr(2U);
        }
    }
    return {};
}

std::vector<std::string> split_branch_labels(std::string_view branch)
{
    std::vector<std::string> labels;
    if (!starts_with(branch, kBranchPrefix))
    {
        return labels;
    }

    std::string_view rest = branch.substr(kBranchPrefix.size());
    while (!rest.empty())
    {
        const auto comma = rest.find(',');
        const auto part = comma == std::string_view::npos ? rest : rest.substr(0U, comma);
        const auto trimmed = trim_copy(part);
        if (!trimmed.empty())
        {
            labels.push_back(trimmed);
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        rest.remove_prefix(comma + 1U);
    }
    return labels;
}

bool append_valid_enr(std::string entry, std::vector<std::string>& out)
{
    if (!starts_with(entry, kEnrPrefix))
    {
        return false;
    }

    const auto record = EnrParser::parse(entry);
    if (!record)
    {
        return false;
    }
    const auto peer = EnrParser::to_validated_peer(record.value());
    if (!peer)
    {
        return false;
    }

    out.push_back(std::move(entry));
    return true;
}

} // namespace

EnrTreeResolver::EnrTreeResolver(DnsTxtLookupFn lookup) noexcept
    : lookup_(std::move(lookup))
{
    if (!lookup_)
    {
        lookup_ = system_txt_lookup;
    }
}

bool EnrTreeResolver::is_enr_tree_url(const std::string& value) noexcept
{
    return starts_with(value, kEnrTreePrefix);
}

bool EnrTreeResolver::parse_url(const std::string& value, EnrTreeUrl& out) noexcept
{
    if (!is_enr_tree_url(value))
    {
        return false;
    }

    const std::string_view rest(value.data() + kEnrTreePrefix.size(), value.size() - kEnrTreePrefix.size());
    const auto at = rest.find('@');
    if (at == std::string_view::npos || at == 0U || at + 1U >= rest.size())
    {
        return false;
    }

    out.public_key = std::string(rest.substr(0U, at));
    out.domain = std::string(rest.substr(at + 1U));
    return !out.public_key.empty() && !out.domain.empty();
}

std::vector<std::string> EnrTreeResolver::system_txt_lookup(const std::string& name) noexcept
{
#if defined(_WIN32)
    DNS_RECORDA* records = nullptr;
    const DNS_STATUS status = DnsQuery_A(
        name.c_str(),
        DNS_TYPE_TEXT,
        DNS_QUERY_STANDARD,
        nullptr,
        &records,
        nullptr);
    if (status != ERROR_SUCCESS)
    {
        if (records != nullptr)
        {
            DnsRecordListFree(records, DnsFreeRecordList);
        }
        return {};
    }

    std::vector<std::string> text_records;
    for (const DNS_RECORDA* record = records; record != nullptr; record = record->pNext)
    {
        if (record->wType != DNS_TYPE_TEXT)
        {
            continue;
        }

        std::string text;
        const auto& txt = record->Data.TXT;
        for (DWORD i = 0; i < txt.dwStringCount; ++i)
        {
            if (txt.pStringArray[i] != nullptr)
            {
                text.append(txt.pStringArray[i]);
            }
        }
        if (!text.empty())
        {
            text_records.push_back(std::move(text));
        }
    }

    if (records != nullptr)
    {
        DnsRecordListFree(records, DnsFreeRecordList);
    }
    return text_records;
#else
    std::array<unsigned char, kMaxDnsResponseBytes> answer{};
    const int len = res_query(
        name.c_str(),
        ns_c_in,
        ns_t_txt,
        answer.data(),
        static_cast<int>(answer.size()));
    if (len <= 0)
    {
        return {};
    }

    ns_msg handle{};
    if (ns_initparse(answer.data(), len, &handle) != 0)
    {
        return {};
    }

    std::vector<std::string> records;
    const int count = ns_msg_count(handle, ns_s_an);
    for (int i = 0; i < count; ++i)
    {
        ns_rr rr{};
        if (ns_parserr(&handle, ns_s_an, i, &rr) != 0 || ns_rr_type(rr) != ns_t_txt)
        {
            continue;
        }

        const unsigned char* data = ns_rr_rdata(rr);
        size_t remaining = ns_rr_rdlen(rr);
        std::string text;
        while (remaining > 0U)
        {
            const size_t chunk = data[0];
            ++data;
            --remaining;
            if (chunk > remaining)
            {
                text.clear();
                break;
            }
            text.append(reinterpret_cast<const char*>(data), chunk);
            data += chunk;
            remaining -= chunk;
        }
        if (!text.empty())
        {
            records.push_back(std::move(text));
        }
    }

    return records;
#endif
}

std::vector<std::string> EnrTreeResolver::resolve(
    const std::vector<std::string>& urls,
    size_t                          max_records) const noexcept
{
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& url_string : urls)
    {
        EnrTreeUrl url{};
        if (!parse_url(url_string, url))
        {
            continue;
        }
        for (auto& enr : resolve_url(url, max_records))
        {
            if (seen.insert(enr).second)
            {
                out.push_back(std::move(enr));
                if (out.size() >= max_records)
                {
                    return out;
                }
            }
        }
    }
    return out;
}

std::vector<std::string> EnrTreeResolver::resolve_url(
    const EnrTreeUrl& url,
    size_t            max_records) const noexcept
{
    std::vector<std::string> out;
    std::deque<std::string> pending_labels;
    std::unordered_set<std::string> seen_labels;

    const auto root_records = lookup_(url.domain);
    for (const auto& root : root_records)
    {
        if (!starts_with(root, kRootPrefix))
        {
            continue;
        }
        const auto label = root_entry_label(root);
        if (label.empty())
        {
            continue;
        }
        pending_labels.push_back(label);
    }

    size_t lookups = 0U;
    while (!pending_labels.empty() && out.size() < max_records && lookups < kMaxTreeLookups)
    {
        std::string label = std::move(pending_labels.front());
        pending_labels.pop_front();
        if (!seen_labels.insert(label).second)
        {
            continue;
        }

        ++lookups;
        const std::string name = label + "." + url.domain;
        for (const auto& raw_entry : lookup_(name))
        {
            const auto entry = trim_copy(raw_entry);
            if (append_valid_enr(entry, out))
            {
                if (out.size() >= max_records)
                {
                    break;
                }
                continue;
            }

            for (const auto& child : split_branch_labels(entry))
            {
                if (seen_labels.count(child) == 0U)
                {
                    pending_labels.push_back(child);
                }
            }
        }
    }

    return out;
}

void EnrTreeResolver::resolve_entry(
    const std::string& domain,
    const std::string& label,
    size_t             max_records,
    std::vector<std::string>& out) const noexcept
{
    if (out.size() >= max_records)
    {
        return;
    }

    const std::string name = label + "." + domain;
    for (const auto& raw_entry : lookup_(name))
    {
        const auto entry = trim_copy(raw_entry);
        if (append_valid_enr(entry, out))
        {
            if (out.size() >= max_records)
            {
                return;
            }
            continue;
        }

        for (const auto& child : split_branch_labels(entry))
        {
            resolve_entry(domain, child, max_records, out);
            if (out.size() >= max_records)
            {
                return;
            }
        }
    }
}

} // namespace discv5
