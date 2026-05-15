// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/json_rpc.hpp>
#include <base/parse_utility.hpp>

namespace eth::rpc {

namespace {

[[nodiscard]] std::optional<uint64_t> parse_quantity_value(const boost::json::value& value)
{
    const auto* string_value = value.if_string();
    if (string_value == nullptr)
    {
        return std::nullopt;
    }
    return rlp::base::parse::uint64_hex(std::string_view(string_value->data(), string_value->size()));
}

template <size_t N>
[[nodiscard]] std::optional<std::array<uint8_t, N>> parse_fixed_hex_value(const boost::json::value& value)
{
    std::array<uint8_t, N> out{};
    const auto* string_value = value.if_string();
    if (string_value == nullptr)
    {
        return std::nullopt;
    }
    if (!rlp::base::parse::hex_array(std::string_view(string_value->data(), string_value->size()), out))
    {
        return std::nullopt;
    }
    return out;
}

[[nodiscard]] std::optional<codec::ByteBuffer> parse_bytes_value(const boost::json::value& value)
{
    const auto* string_value = value.if_string();
    if (string_value == nullptr)
    {
        return std::nullopt;
    }

    return rlp::base::parse::hex_bytes(
        std::string_view(string_value->data(), string_value->size()));
}

[[nodiscard]] const boost::json::object* response_result_object(const boost::json::value& parsed)
{
    const auto* root = parsed.if_object();
    if (root == nullptr)
    {
        return nullptr;
    }

    const auto* result = root->if_contains("result");
    if (result == nullptr)
    {
        return nullptr;
    }
    return result->if_object();
}

[[nodiscard]] std::optional<RpcLog> parse_rpc_log(const boost::json::object& object)
{
    const auto* address_value = object.if_contains("address");
    const auto* topics_value = object.if_contains("topics");
    const auto* data_value = object.if_contains("data");
    const auto* block_number_value = object.if_contains("blockNumber");
    const auto* block_hash_value = object.if_contains("blockHash");
    const auto* tx_hash_value = object.if_contains("transactionHash");
    const auto* log_index_value = object.if_contains("logIndex");
    if (address_value == nullptr || topics_value == nullptr || data_value == nullptr
        || block_number_value == nullptr || block_hash_value == nullptr
        || tx_hash_value == nullptr || log_index_value == nullptr)
    {
        return std::nullopt;
    }

    const auto address = parse_fixed_hex_value<20>(*address_value);
    const auto data = parse_bytes_value(*data_value);
    const auto block_number = parse_quantity_value(*block_number_value);
    const auto block_hash = parse_fixed_hex_value<32>(*block_hash_value);
    const auto tx_hash = parse_fixed_hex_value<32>(*tx_hash_value);
    const auto log_index = parse_quantity_value(*log_index_value);
    const auto* topics_array = topics_value->if_array();
    if (!address.has_value() || !data.has_value() || !block_number.has_value()
        || !block_hash.has_value() || !tx_hash.has_value() || !log_index.has_value()
        || topics_array == nullptr || *log_index > UINT32_MAX)
    {
        return std::nullopt;
    }

    RpcLog parsed;
    parsed.log.address = *address;
    parsed.log.data = *data;
    parsed.block_number = *block_number;
    parsed.block_hash = *block_hash;
    parsed.tx_hash = *tx_hash;
    parsed.log_index = static_cast<uint32_t>(*log_index);

    parsed.log.topics.reserve(topics_array->size());
    for (const auto& topic_value : *topics_array)
    {
        auto topic = parse_fixed_hex_value<32>(topic_value);
        if (!topic.has_value())
        {
            return std::nullopt;
        }
        parsed.log.topics.push_back(*topic);
    }
    return parsed;
}

[[nodiscard]] std::optional<boost::json::value> parse_json(std::string_view json_text)
{
    boost::system::error_code ec;
    auto parsed = boost::json::parse(json_text, ec);
    if (ec)
    {
        return std::nullopt;
    }
    return parsed;
}

} // namespace

std::string_view block_tag_name(RpcBlockTag tag) noexcept
{
    switch (tag)
    {
        case RpcBlockTag::kLatest: return "latest";
        case RpcBlockTag::kSafe: return "safe";
        case RpcBlockTag::kFinalized: return "finalized";
    }
    return "latest";
}

boost::json::object make_json_rpc_request(
    std::string_view   method,
    boost::json::array params,
    uint64_t           id)
{
    boost::json::object request;
    request["jsonrpc"] = "2.0";
    request["method"] = method;
    request["params"] = std::move(params);
    request["id"] = id;
    return request;
}

boost::json::object make_get_block_by_number_request(RpcBlockTag tag, uint64_t id)
{
    boost::json::array params;
    params.emplace_back(block_tag_name(tag));
    params.emplace_back(false);
    return make_json_rpc_request("eth_getBlockByNumber", std::move(params), id);
}

boost::json::object make_get_logs_request(
    const EventFilter& filter,
    uint64_t           from_block,
    uint64_t           to_block,
    uint64_t           id)
{
    boost::json::object filter_json;
    filter_json["fromBlock"] = rlp::base::parse::uint64_hex_quantity(from_block);
    filter_json["toBlock"] = rlp::base::parse::uint64_hex_quantity(to_block);

    if (filter.addresses.size() == 1)
    {
        filter_json["address"] = rlp::base::parse::hex_array_string(filter.addresses.front());
    }
    else if (!filter.addresses.empty())
    {
        boost::json::array addresses;
        for (const auto& address : filter.addresses)
        {
            addresses.emplace_back(rlp::base::parse::hex_array_string(address));
        }
        filter_json["address"] = std::move(addresses);
    }

    if (!filter.topics.empty())
    {
        boost::json::array topics;
        for (const auto& topic : filter.topics)
        {
            if (!topic.has_value())
            {
                topics.emplace_back(nullptr);
                continue;
            }
            topics.emplace_back(rlp::base::parse::hex_array_string(*topic));
        }
        filter_json["topics"] = std::move(topics);
    }

    boost::json::array params;
    params.emplace_back(std::move(filter_json));
    return make_json_rpc_request("eth_getLogs", std::move(params), id);
}

boost::json::object make_get_transaction_receipt_request(
    const Hash256& tx_hash,
    uint64_t       id)
{
    boost::json::array params;
    params.emplace_back(rlp::base::parse::hex_array_string(tx_hash));
    return make_json_rpc_request("eth_getTransactionReceipt", std::move(params), id);
}

std::optional<uint64_t> parse_block_number_response(std::string_view json_text)
{
    const auto parsed = parse_json(json_text);
    if (!parsed.has_value())
    {
        return std::nullopt;
    }

    const auto* result = response_result_object(*parsed);
    if (result == nullptr)
    {
        return std::nullopt;
    }

    const auto* number = result->if_contains("number");
    if (number == nullptr)
    {
        return std::nullopt;
    }
    return parse_quantity_value(*number);
}

std::optional<std::vector<RpcLog>> parse_get_logs_response(std::string_view json_text)
{
    const auto parsed = parse_json(json_text);
    if (!parsed.has_value())
    {
        return std::nullopt;
    }

    const auto* root = parsed->if_object();
    if (root == nullptr)
    {
        return std::nullopt;
    }

    const auto* result = root->if_contains("result");
    if (result == nullptr)
    {
        return std::nullopt;
    }

    const auto* logs = result->if_array();
    if (logs == nullptr)
    {
        return std::nullopt;
    }

    std::vector<RpcLog> parsed_logs;
    parsed_logs.reserve(logs->size());
    for (const auto& log_value : *logs)
    {
        const auto* log_object = log_value.if_object();
        if (log_object == nullptr)
        {
            return std::nullopt;
        }
        auto log = parse_rpc_log(*log_object);
        if (!log.has_value())
        {
            return std::nullopt;
        }
        parsed_logs.push_back(std::move(*log));
    }
    return parsed_logs;
}

std::optional<ReceiptResult> parse_transaction_receipt_response(std::string_view json_text)
{
    const auto parsed = parse_json(json_text);
    if (!parsed.has_value())
    {
        return std::nullopt;
    }

    const auto* receipt_object = response_result_object(*parsed);
    if (receipt_object == nullptr)
    {
        return std::nullopt;
    }

    const auto* status_value = receipt_object->if_contains("status");
    const auto* block_number_value = receipt_object->if_contains("blockNumber");
    const auto* block_hash_value = receipt_object->if_contains("blockHash");
    const auto* tx_hash_value = receipt_object->if_contains("transactionHash");
    const auto* logs_value = receipt_object->if_contains("logs");
    if (status_value == nullptr || block_number_value == nullptr || block_hash_value == nullptr
        || tx_hash_value == nullptr || logs_value == nullptr)
    {
        return std::nullopt;
    }

    const auto status = parse_quantity_value(*status_value);
    const auto block_number = parse_quantity_value(*block_number_value);
    const auto block_hash = parse_fixed_hex_value<32>(*block_hash_value);
    const auto tx_hash = parse_fixed_hex_value<32>(*tx_hash_value);
    const auto* logs = logs_value->if_array();
    if (!status.has_value() || !block_number.has_value() || !block_hash.has_value()
        || !tx_hash.has_value() || logs == nullptr)
    {
        return std::nullopt;
    }

    ReceiptResult result;
    result.receipt.status = (*status != 0);
    result.block_number = *block_number;
    result.block_hash = *block_hash;
    result.tx_hash = *tx_hash;
    result.receipt.logs.reserve(logs->size());
    result.log_indices.reserve(logs->size());

    for (const auto& log_value : *logs)
    {
        const auto* log_object = log_value.if_object();
        if (log_object == nullptr)
        {
            return std::nullopt;
        }
        auto log = parse_rpc_log(*log_object);
        if (!log.has_value())
        {
            return std::nullopt;
        }
        result.receipt.logs.push_back(std::move(log->log));
        result.log_indices.push_back(log->log_index);
    }
    return result;
}

} // namespace eth::rpc
