// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/rpc_manager_config.hpp>

#include <base/json_utility.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace eth::rpc {

namespace {

namespace json = rlp::base::json;

[[nodiscard]] const json::JsonSchemaObject& endpoint_schema()
{
    static const json::JsonSchemaObject kEndpointSchema{{
        {"chainName", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"chainId", json::JsonFieldType::kU64, true, std::nullopt, nullptr, nullptr},
        {"urlTemplate", json::JsonFieldType::kString, true, std::nullopt, nullptr, nullptr},
        {"apiKeyEnvVar", json::JsonFieldType::kString, false, std::nullopt, nullptr, nullptr},
        {"apiKeyLiteral", json::JsonFieldType::kString, false, std::nullopt, nullptr, nullptr},
        {"priority", json::JsonFieldType::kU32, true, std::nullopt, nullptr, nullptr},
        {"weight", json::JsonFieldType::kU32, true, std::nullopt, nullptr, nullptr},
        {"rateLimitPerSecond", json::JsonFieldType::kU32, true, std::nullopt, nullptr, nullptr},
        {"paid", json::JsonFieldType::kBool, false, boost::json::value( false ), nullptr, nullptr},
        {"public", json::JsonFieldType::kBool, false, boost::json::value( true ), nullptr, nullptr},
        {"verified", json::JsonFieldType::kBool, false, boost::json::value( false ), nullptr, nullptr}
    }};
    return kEndpointSchema;
}

[[nodiscard]] const json::JsonSchemaObject& rpc_manager_schema()
{
    static const json::JsonSchemaArray kEndpointsSchema{
        json::JsonFieldType::kObject,
        &endpoint_schema(),
        nullptr
    };
    static const json::JsonSchemaObject kRpcManagerSchema{{
        {"maxEndpointsPerChain", json::JsonFieldType::kSize, true, std::nullopt, nullptr, nullptr},
        {"endpoints", json::JsonFieldType::kArray, true, std::nullopt, nullptr, &kEndpointsSchema}
    }};
    return kRpcManagerSchema;
}

template <typename T>
[[nodiscard]] json::JsonResult<T> fail_from(const json::JsonError& error)
{
    return json::outcome::failure( error );
}

[[nodiscard]] json::JsonResult<RpcEndpointConfig> build_endpoint(const json::JsonParsedObject& object)
{
    RpcEndpointConfig endpoint;

    const auto chain_name = json::get_parsed_string( object, "chainName" );
    if ( !chain_name )
    {
        return fail_from<RpcEndpointConfig>( chain_name.error() );
    }
    endpoint.chain_name = chain_name.value();

    const auto chain_id = json::get_parsed_u64( object, "chainId" );
    if ( !chain_id )
    {
        return fail_from<RpcEndpointConfig>( chain_id.error() );
    }
    endpoint.chain_id = chain_id.value();

    const auto url_template = json::get_parsed_string( object, "urlTemplate" );
    if ( !url_template )
    {
        return fail_from<RpcEndpointConfig>( url_template.error() );
    }
    endpoint.url_template = url_template.value();

    if ( object.find( "apiKeyEnvVar" ) != nullptr )
    {
        const auto api_key_env_var = json::get_parsed_string( object, "apiKeyEnvVar" );
        if ( !api_key_env_var )
        {
            return fail_from<RpcEndpointConfig>( api_key_env_var.error() );
        }
        endpoint.api_key_env_var = api_key_env_var.value();
    }

    if ( object.find( "apiKeyLiteral" ) != nullptr )
    {
        const auto api_key_literal = json::get_parsed_string( object, "apiKeyLiteral" );
        if ( !api_key_literal )
        {
            return fail_from<RpcEndpointConfig>( api_key_literal.error() );
        }
        endpoint.api_key_literal = api_key_literal.value();
    }

    const auto priority = json::get_parsed_u32( object, "priority" );
    if ( !priority )
    {
        return fail_from<RpcEndpointConfig>( priority.error() );
    }
    endpoint.priority = priority.value();

    const auto weight = json::get_parsed_u32( object, "weight" );
    if ( !weight )
    {
        return fail_from<RpcEndpointConfig>( weight.error() );
    }
    endpoint.weight = weight.value();

    const auto rate_limit = json::get_parsed_u32( object, "rateLimitPerSecond" );
    if ( !rate_limit )
    {
        return fail_from<RpcEndpointConfig>( rate_limit.error() );
    }
    endpoint.rate_limit_per_second = rate_limit.value();

    const auto paid = json::get_parsed_bool( object, "paid" );
    if ( !paid )
    {
        return fail_from<RpcEndpointConfig>( paid.error() );
    }
    endpoint.is_paid = paid.value();

    const auto public_endpoint = json::get_parsed_bool( object, "public" );
    if ( !public_endpoint )
    {
        return fail_from<RpcEndpointConfig>( public_endpoint.error() );
    }
    endpoint.is_public = public_endpoint.value();

    const auto verified = json::get_parsed_bool( object, "verified" );
    if ( !verified )
    {
        return fail_from<RpcEndpointConfig>( verified.error() );
    }
    endpoint.verified = verified.value();

    return endpoint;
}

} // namespace

json::JsonResult<RpcManagerConfig> load_rpc_manager_config_result_from_json_text(
    const std::string& json_text)
{
    const auto root = json::parse_schema_object( json_text, rpc_manager_schema() );
    if ( !root )
    {
        return json::outcome::failure( root.error() );
    }

    const auto max_endpoints = json::get_parsed_size( root.value(), "maxEndpointsPerChain" );
    if ( !max_endpoints )
    {
        return json::outcome::failure( max_endpoints.error() );
    }
    const auto endpoints = json::get_parsed_array( root.value(), "endpoints" );
    if ( !endpoints )
    {
        return json::outcome::failure( endpoints.error() );
    }

    RpcManagerConfig config;
    config.max_endpoints_per_chain = max_endpoints.value();

    for ( const auto& endpoint_value : endpoints.value()->values )
    {
        const auto* endpoint_object = std::get_if<json::JsonParsedObjectPtr>( &endpoint_value.value );
        if ( endpoint_object == nullptr || *endpoint_object == nullptr )
        {
            return json::outcome::failure( json::JsonError{json::JsonErrorCode::kWrongType, "endpoints[]"} );
        }
        const auto endpoint = build_endpoint( **endpoint_object );
        if ( !endpoint )
        {
            return json::outcome::failure( endpoint.error() );
        }
        config.endpoints.push_back( endpoint.value() );
    }

    return config;
}

json::JsonResult<RpcManagerConfig> load_rpc_manager_config_result_from_json(
    const std::filesystem::path& json_path)
{
    std::ifstream input( json_path, std::ios::in | std::ios::binary );
    if ( !input.is_open() )
    {
        return json::outcome::failure( json::JsonError{json::JsonErrorCode::kFileOpenFailed, json_path.string()} );
    }

    const std::string json_text{
        std::istreambuf_iterator<char>( input ),
        std::istreambuf_iterator<char>()};
    return load_rpc_manager_config_result_from_json_text( json_text );
}

std::optional<RpcManagerConfig> load_rpc_manager_config_from_json_text(
    const std::string& json_text)
{
    const auto config = load_rpc_manager_config_result_from_json_text( json_text );
    if ( !config )
    {
        return std::nullopt;
    }
    return config.value();
}

std::optional<RpcManagerConfig> load_rpc_manager_config_from_json(
    const std::filesystem::path& json_path)
{
    const auto config = load_rpc_manager_config_result_from_json( json_path );
    if ( !config )
    {
        return std::nullopt;
    }
    return config.value();
}

} // namespace eth::rpc
