// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_JSON_RPC_HPP
#define EVMRELAY_INCLUDE_ETH_JSON_RPC_HPP

#include <eth/eth_receipt_source.hpp>
#include <boost/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eth::rpc {

enum class RpcBlockTag
{
    kLatest,
    kSafe,
    kFinalized,
};

struct RpcLog
{
    codec::LogEntry log;
    uint64_t        block_number = 0;
    Hash256         block_hash{};
    Hash256         tx_hash{};
    uint32_t        log_index = 0;
};

[[nodiscard]] std::string_view block_tag_name(RpcBlockTag tag) noexcept;

[[nodiscard]] boost::json::object make_json_rpc_request(
    std::string_view    method,
    boost::json::array  params,
    uint64_t            id);

[[nodiscard]] boost::json::object make_get_block_by_number_request(
    RpcBlockTag tag,
    uint64_t    id);

[[nodiscard]] boost::json::object make_get_logs_request(
    const EventFilter& filter,
    uint64_t           from_block,
    uint64_t           to_block,
    uint64_t           id);

[[nodiscard]] boost::json::object make_get_transaction_receipt_request(
    const Hash256& tx_hash,
    uint64_t       id);

[[nodiscard]] std::optional<uint64_t> parse_block_number_response(std::string_view json_text);
[[nodiscard]] std::optional<std::vector<RpcLog>> parse_get_logs_response(std::string_view json_text);
[[nodiscard]] std::optional<ReceiptResult> parse_transaction_receipt_response(std::string_view json_text);

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_JSON_RPC_HPP
