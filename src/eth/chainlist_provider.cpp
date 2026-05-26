// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/chainlist_provider.hpp>

#include <base/json_utility.hpp>

#include <boost/json.hpp>
#include <base/rlp-logger.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

namespace eth::rpc {

namespace {

namespace json = rlp::base::json;

template <typename T>
[[nodiscard]] json::JsonResult<T> fail_from(const json::JsonError& error)
{
    return json::outcome::failure( error );
}

[[nodiscard]] const json::JsonSchemaObject& chain_entry_schema()
{
    static const json::JsonSchemaArray kRpcArraySchema{
        json::JsonFieldType::kString,
        nullptr,
        nullptr
    };
    static const json::JsonSchemaObject kChainEntrySchema{{
        {"name",     json::JsonFieldType::kString, true,  std::nullopt, nullptr, nullptr},
        {"chainId",  json::JsonFieldType::kU64,    true,  std::nullopt, nullptr, nullptr},
        {"rpc",      json::JsonFieldType::kArray,  true,  std::nullopt, nullptr, &kRpcArraySchema},
        {"status",   json::JsonFieldType::kString, false, boost::json::value( "active" ), nullptr, nullptr},
        {"shortName", json::JsonFieldType::kString, false, boost::json::value( "" ), nullptr, nullptr}
    }};
    return kChainEntrySchema;
}

[[nodiscard]] inline bool is_filtered_url(std::string_view url) noexcept
{
    if ( url.empty() )
    {
        return true;
    }

    constexpr std::string_view kApiKeyPlaceholders[] = {
        "${INFURA_API_KEY}",
        "${ALCHEMY_API_KEY}",
        "${ANKR_API_KEY}",
        "${POKT_API_KEY}",
        "${BLASTAPI_API_KEY}"
    };

    for ( const auto& placeholder : kApiKeyPlaceholders )
    {
        if ( url.find( placeholder ) != std::string_view::npos )
        {
            return true;
        }
    }

    if ( url.find( "wss://" ) == 0 || url.find( "ws://" ) == 0 )
    {
        return true;
    }

    if ( url.find( "https://" ) != 0 && url.find( "http://" ) != 0 )
    {
        return true;
    }

    return false;
}

[[nodiscard]] json::JsonResult<std::vector<RpcEndpointConfig>> parse_chain_entries(
    const boost::json::array& entries)
{
    std::vector<RpcEndpointConfig> result;
    std::unordered_set<std::string> seen;

    for ( const auto& element : entries )
    {
        if ( !element.is_object() )
        {
            continue;
        }

        const auto& obj = element.as_object();
        const auto parsed = json::parse_schema_object( obj, chain_entry_schema() );
        if ( !parsed )
        {
            auto logger = rlp::base::createLogger( "chainlist_provider" );
            logger->warn( "Skipping chain entry: {}", parsed.error().field );
            continue;
        }

        const auto& entry = parsed.value();

        const auto status = json::get_parsed_string( entry, "status" );
        if ( !status )
        {
            continue;
        }
        if ( status.value() == "deprecated" )
        {
            continue;
        }

        const auto name = json::get_parsed_string( entry, "name" );
        if ( !name )
        {
            continue;
        }

        const auto chain_id = json::get_parsed_u64( entry, "chainId" );
        if ( !chain_id )
        {
            continue;
        }

        const auto rpc_urls = json::get_parsed_array( entry, "rpc" );
        if ( !rpc_urls )
        {
            continue;
        }

        for ( const auto& url_value : rpc_urls.value()->values )
        {
            const auto* url_str = std::get_if<std::string>( &url_value.value );
            if ( url_str == nullptr || url_str->empty() )
            {
                continue;
            }

            if ( is_filtered_url( *url_str ) )
            {
                continue;
            }

            const auto dedup_key = std::to_string( chain_id.value() ) + "|" + *url_str;
            if ( seen.find( dedup_key ) != seen.end() )
            {
                continue;
            }
            seen.insert( dedup_key );

            RpcEndpointConfig endpoint;
            endpoint.chain_name = name.value();
            endpoint.chain_id = chain_id.value();
            endpoint.url_template = *url_str;
            endpoint.priority = 0;
            endpoint.weight = 0;
            endpoint.rate_limit_per_second = 0;
            endpoint.is_paid = false;
            endpoint.is_public = true;
            endpoint.verified = false;
            result.push_back( std::move( endpoint ) );
        }
    }

    return result;
}

} // namespace

json::JsonResult<std::vector<RpcEndpointConfig>> load_chainlist_from_json_text(
    std::string_view json_text)
{
    boost::system::error_code ec;
    const auto root = boost::json::parse( std::string( json_text ), ec );
    if ( ec )
    {
        return json::outcome::failure(
            json::JsonError{json::JsonErrorCode::kParseFailed, {}} );
    }

    if ( !root.is_array() )
    {
        return json::outcome::failure(
            json::JsonError{json::JsonErrorCode::kRootNotObject, {}} );
    }

    return parse_chain_entries( root.as_array() );
}

std::vector<RpcEndpointConfig> filter_to_configured_chains(
    std::vector<RpcEndpointConfig>        endpoints,
    const std::vector<uint64_t>&          configured_chain_ids)
{
    std::unordered_set<uint64_t> configured(
        configured_chain_ids.begin(),
        configured_chain_ids.end() );

    endpoints.erase(
        std::remove_if( endpoints.begin(), endpoints.end(),
            [&configured]( const RpcEndpointConfig& ep )
            {
                return configured.find( ep.chain_id ) == configured.end();
            } ),
        endpoints.end() );

    return endpoints;
}

} // namespace eth::rpc
