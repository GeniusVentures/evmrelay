// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/json_rpc.hpp>
#include <base/json_utility.hpp>
#include <base/parse_utility.hpp>

namespace eth::rpc {

namespace {

namespace json = rlp::base::json;

[[nodiscard]] std::optional<uint64_t> parse_quantity_value(std::string_view value)
{
    return rlp::base::parse::uint64_hex(value);
}

template <size_t N>
[[nodiscard]] std::optional<std::array<uint8_t, N>> parse_fixed_hex_value(std::string_view value)
{
    std::array<uint8_t, N> out{};
    if (!rlp::base::parse::hex_array(value, out))
    {
        return std::nullopt;
    }
    return out;
}

[[nodiscard]] std::optional<codec::ByteBuffer> parse_bytes_value(std::string_view value)
{
    return rlp::base::parse::hex_bytes(value);
}

[[nodiscard]] const json::JsonSchemaObject& block_result_schema()
{
    static const json::JsonSchemaObject kSchema{{
        {"number", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr}
    }};
    return kSchema;
}

[[nodiscard]] const json::JsonSchemaArray& log_topics_schema()
{
    static const json::JsonSchemaArray kSchema{json::JsonFieldType::kString, nullptr, nullptr};
    return kSchema;
}

[[nodiscard]] const json::JsonSchemaObject& rpc_log_schema()
{
    static const json::JsonSchemaObject kSchema{{
        {"address", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"topics", json::JsonFieldType::kArray, true, std::nullopt, nullptr, &log_topics_schema()},
        {"data", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"blockNumber", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"blockHash", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"transactionHash", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"logIndex", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr}
    }};
    return kSchema;
}

[[nodiscard]] const json::JsonSchemaArray& logs_schema()
{
    static const json::JsonSchemaArray kSchema{json::JsonFieldType::kObject, &rpc_log_schema(), nullptr};
    return kSchema;
}

[[nodiscard]] const json::JsonSchemaObject& logs_response_schema()
{
    static const json::JsonSchemaObject kSchema{{
        {"result", json::JsonFieldType::kArray, true, std::nullopt, nullptr, &logs_schema()}
    }};
    return kSchema;
}

[[nodiscard]] const json::JsonSchemaObject& block_response_schema()
{
    static const json::JsonSchemaObject kSchema{{
        {"result", json::JsonFieldType::kObject, true, std::nullopt, &block_result_schema(), nullptr}
    }};
    return kSchema;
}

[[nodiscard]] const json::JsonSchemaObject& receipt_result_schema()
{
    static const json::JsonSchemaObject kSchema{{
        {"status", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"blockNumber", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"blockHash", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"transactionHash", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"logs", json::JsonFieldType::kArray, true, std::nullopt, nullptr, &logs_schema()}
    }};
    return kSchema;
}

[[nodiscard]] const json::JsonSchemaObject& receipt_response_schema()
{
    static const json::JsonSchemaObject kSchema{{
        {"result", json::JsonFieldType::kObject, true, std::nullopt, &receipt_result_schema(), nullptr}
    }};
    return kSchema;
}

[[nodiscard]] std::optional<RpcLog> parse_rpc_log(const json::JsonParsedObject& object)
{
    const auto address_value = json::get_parsed_string(object, "address");
    const auto topics_value = json::get_parsed_array(object, "topics");
    const auto data_value = json::get_parsed_string(object, "data");
    const auto block_number_value = json::get_parsed_string(object, "blockNumber");
    const auto block_hash_value = json::get_parsed_string(object, "blockHash");
    const auto tx_hash_value = json::get_parsed_string(object, "transactionHash");
    const auto log_index_value = json::get_parsed_string(object, "logIndex");
    if (!address_value || !topics_value || !data_value || !block_number_value ||
        !block_hash_value || !tx_hash_value || !log_index_value)
    {
        return std::nullopt;
    }

    const auto address = parse_fixed_hex_value<20>(address_value.value());
    const auto data = parse_bytes_value(data_value.value());
    const auto block_number = parse_quantity_value(block_number_value.value());
    const auto block_hash = parse_fixed_hex_value<32>(block_hash_value.value());
    const auto tx_hash = parse_fixed_hex_value<32>(tx_hash_value.value());
    const auto log_index = parse_quantity_value(log_index_value.value());
    if (!address.has_value() || !data.has_value() || !block_number.has_value()
        || !block_hash.has_value() || !tx_hash.has_value() || !log_index.has_value()
        || *log_index > UINT32_MAX)
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

    parsed.log.topics.reserve(topics_value.value()->values.size());
    for (const auto& topic_value : topics_value.value()->values)
    {
        const auto* topic_string = std::get_if<std::string>(&topic_value.value);
        if (topic_string == nullptr)
        {
            return std::nullopt;
        }
        auto topic = parse_fixed_hex_value<32>(*topic_string);
        if (!topic.has_value())
        {
            return std::nullopt;
        }
        parsed.log.topics.push_back(*topic);
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

boost::json::object make_eth_chain_id_request(uint64_t id)
{
    boost::json::array params;
    return make_json_rpc_request("eth_chainId", std::move(params), id);
}

std::optional<uint64_t> parse_chain_id_response(std::string_view json_text)
{
    static const json::JsonSchemaObject kChainIdResponseSchema{{
        {"result", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr}
    }};
    const auto parsed = json::parse_schema_object(json_text, kChainIdResponseSchema);
    if (!parsed)
    {
        return std::nullopt;
    }
    const auto chain_id_hex = json::get_parsed_string(parsed.value(), "result");
    if (!chain_id_hex)
    {
        return std::nullopt;
    }
    return parse_quantity_value(chain_id_hex.value());
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
    const auto parsed = json::parse_schema_object(json_text, block_response_schema());
    if (!parsed)
    {
        return std::nullopt;
    }

    const auto result = json::get_parsed_object(parsed.value(), "result");
    if (!result)
    {
        return std::nullopt;
    }
    const auto number = json::get_parsed_string(*result.value(), "number");
    if (!number)
    {
        return std::nullopt;
    }
    return parse_quantity_value(number.value());
}

std::optional<std::vector<RpcLog>> parse_get_logs_response(std::string_view json_text)
{
    const auto parsed = json::parse_schema_object(json_text, logs_response_schema());
    if (!parsed)
    {
        return std::nullopt;
    }

    const auto logs = json::get_parsed_array(parsed.value(), "result");
    if (!logs)
    {
        return std::nullopt;
    }

    std::vector<RpcLog> parsed_logs;
    parsed_logs.reserve(logs.value()->values.size());
    for (const auto& log_value : logs.value()->values)
    {
        const auto* log_object = std::get_if<json::JsonParsedObjectPtr>(&log_value.value);
        if (log_object == nullptr || *log_object == nullptr)
        {
            return std::nullopt;
        }
        auto log = parse_rpc_log(**log_object);
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
    const auto parsed = json::parse_schema_object(json_text, receipt_response_schema());
    if (!parsed)
    {
        return std::nullopt;
    }

    const auto receipt_object = json::get_parsed_object(parsed.value(), "result");
    if (!receipt_object)
    {
        return std::nullopt;
    }

    const auto status_value = json::get_parsed_string(*receipt_object.value(), "status");
    const auto block_number_value = json::get_parsed_string(*receipt_object.value(), "blockNumber");
    const auto block_hash_value = json::get_parsed_string(*receipt_object.value(), "blockHash");
    const auto tx_hash_value = json::get_parsed_string(*receipt_object.value(), "transactionHash");
    const auto logs = json::get_parsed_array(*receipt_object.value(), "logs");
    if (!status_value || !block_number_value || !block_hash_value || !tx_hash_value || !logs)
    {
        return std::nullopt;
    }

    const auto status = parse_quantity_value(status_value.value());
    const auto block_number = parse_quantity_value(block_number_value.value());
    const auto block_hash = parse_fixed_hex_value<32>(block_hash_value.value());
    const auto tx_hash = parse_fixed_hex_value<32>(tx_hash_value.value());
    if (!status.has_value() || !block_number.has_value() || !block_hash.has_value()
        || !tx_hash.has_value())
    {
        return std::nullopt;
    }

    ReceiptResult result;
    result.receipt.status = (*status != 0);
    result.block_number = *block_number;
    result.block_hash = *block_hash;
    result.tx_hash = *tx_hash;
    result.receipt.logs.reserve(logs.value()->values.size());
    result.log_indices.reserve(logs.value()->values.size());

    for (const auto& log_value : logs.value()->values)
    {
        const auto* log_object = std::get_if<json::JsonParsedObjectPtr>(&log_value.value);
        if (log_object == nullptr || *log_object == nullptr)
        {
            return std::nullopt;
        }
        auto log = parse_rpc_log(**log_object);
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
