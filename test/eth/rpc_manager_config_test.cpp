// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <eth/rpc_manager_config.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

namespace {

std::filesystem::path write_file(
    const std::filesystem::path& path,
    const std::string&           contents)
{
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    out << contents;
    return path;
}

} // namespace

TEST(RpcManagerConfigTest, LoadsValidConfigFromJsonText)
{
    const std::string json_text =
        "{"
        "\"maxEndpointsPerChain\":2,"
        "\"endpoints\":["
        "{"
        "\"chainName\":\"ethereum-mainnet\","
        "\"chainId\":1,"
        "\"urlTemplate\":\"https://eth.example/v1/{key}\","
        "\"apiKeyEnvVar\":\"ETH_RPC_KEY\","
        "\"priority\":10,"
        "\"weight\":5,"
        "\"rateLimitPerSecond\":20,"
        "\"paid\":true,"
        "\"public\":false,"
        "\"verified\":true"
        "},"
        "{"
        "\"chainName\":\"ethereum-sepolia\","
        "\"chainId\":11155111,"
        "\"urlTemplate\":\"https://sepolia.example/rpc\","
        "\"priority\":1,"
        "\"weight\":1,"
        "\"rateLimitPerSecond\":5,"
        "\"paid\":false,"
        "\"public\":true,"
        "\"verified\":false"
        "}"
        "]"
        "}";

    const auto config = eth::rpc::load_rpc_manager_config_from_json_text( json_text );

    ASSERT_TRUE( config.has_value() );
    EXPECT_EQ( config->max_endpoints_per_chain, 2U );
    ASSERT_EQ( config->endpoints.size(), 2U );
    EXPECT_EQ( config->endpoints[0].chain_name, "ethereum-mainnet" );
    EXPECT_EQ( config->endpoints[0].chain_id, 1U );
    EXPECT_TRUE( config->endpoints[0].api_key_env_var.has_value() );
    EXPECT_EQ( *config->endpoints[0].api_key_env_var, "ETH_RPC_KEY" );
    EXPECT_TRUE( config->endpoints[0].is_paid );
    EXPECT_FALSE( config->endpoints[0].is_public );
    EXPECT_TRUE( config->endpoints[0].verified );
}

TEST(RpcManagerConfigTest, LoadsValidConfigFromJsonFile)
{
    const auto temp_dir = std::filesystem::temp_directory_path() / "rpc_manager_config_test";
    std::filesystem::create_directories( temp_dir );
    const auto json_path = write_file(
        temp_dir / "rpc_manager_config.json",
        "{"
        "\"maxEndpointsPerChain\":1,"
        "\"endpoints\":[{"
        "\"chainName\":\"base-mainnet\","
        "\"chainId\":8453,"
        "\"urlTemplate\":\"https://base.example/rpc\","
        "\"priority\":3,"
        "\"weight\":2,"
        "\"rateLimitPerSecond\":15,"
        "\"paid\":false,"
        "\"public\":true,"
        "\"verified\":true"
        "}]}");

    const auto config = eth::rpc::load_rpc_manager_config_from_json( json_path );

    ASSERT_TRUE( config.has_value() );
    ASSERT_EQ( config->endpoints.size(), 1U );
    EXPECT_EQ( config->endpoints[0].chain_name, "base-mainnet" );
}

TEST(RpcManagerConfigTest, RejectsMalformedOrIncompleteConfig)
{
    EXPECT_FALSE( eth::rpc::load_rpc_manager_config_from_json_text( "not json" ).has_value() );

    const std::string missing_field_json =
        "{"
        "\"maxEndpointsPerChain\":1,"
        "\"endpoints\":[{"
        "\"chainName\":\"ethereum-mainnet\","
        "\"chainId\":1,"
        "\"priority\":10,"
        "\"weight\":5,"
        "\"rateLimitPerSecond\":20,"
        "\"paid\":true,"
        "\"public\":false,"
        "\"verified\":true"
        "}]}";

    EXPECT_FALSE( eth::rpc::load_rpc_manager_config_from_json_text( missing_field_json ).has_value() );

    const std::string wrong_type_json =
        "{"
        "\"maxEndpointsPerChain\":1,"
        "\"endpoints\":[{"
        "\"chainName\":\"ethereum-mainnet\","
        "\"chainId\":1,"
        "\"urlTemplate\":\"https://eth.example\","
        "\"priority\":\"high\","
        "\"weight\":5,"
        "\"rateLimitPerSecond\":20,"
        "\"paid\":true,"
        "\"public\":false,"
        "\"verified\":true"
        "}]}";

    EXPECT_FALSE( eth::rpc::load_rpc_manager_config_from_json_text( wrong_type_json ).has_value() );
}

TEST(RpcManagerConfigTest, ResultReportsJsonErrorReason)
{
    const std::string missing_field_json =
        "{"
        "\"maxEndpointsPerChain\":1,"
        "\"endpoints\":[{"
        "\"chainName\":\"ethereum-mainnet\","
        "\"chainId\":1,"
        "\"priority\":10,"
        "\"weight\":5,"
        "\"rateLimitPerSecond\":20,"
        "\"paid\":true,"
        "\"public\":false,"
        "\"verified\":true"
        "}]}";

    const auto missing_field = eth::rpc::load_rpc_manager_config_result_from_json_text( missing_field_json );

    ASSERT_FALSE( missing_field );
    EXPECT_EQ( missing_field.error().code, rlp::base::json::JsonErrorCode::kMissingField );
    EXPECT_EQ( missing_field.error().field, "endpoints[0].urlTemplate" );

    const std::string wrong_type_json =
        "{"
        "\"maxEndpointsPerChain\":1,"
        "\"endpoints\":[{"
        "\"chainName\":\"ethereum-mainnet\","
        "\"chainId\":1,"
        "\"urlTemplate\":\"https://eth.example\","
        "\"priority\":\"high\","
        "\"weight\":5,"
        "\"rateLimitPerSecond\":20,"
        "\"paid\":true,"
        "\"public\":false,"
        "\"verified\":true"
        "}]}";

    const auto wrong_type = eth::rpc::load_rpc_manager_config_result_from_json_text( wrong_type_json );

    ASSERT_FALSE( wrong_type );
    EXPECT_EQ( wrong_type.error().code, rlp::base::json::JsonErrorCode::kWrongType );
    EXPECT_EQ( wrong_type.error().field, "endpoints[0].priority" );
}

TEST(RpcManagerConfigTest, ReportsOutOfRangeInteger)
{
    const std::string out_of_range_json =
        "{"
        "\"maxEndpointsPerChain\":1,"
        "\"endpoints\":[{"
        "\"chainName\":\"ethereum-mainnet\","
        "\"chainId\":1,"
        "\"urlTemplate\":\"https://eth.example\","
        "\"priority\":4294967296,"
        "\"weight\":5,"
        "\"rateLimitPerSecond\":20,"
        "\"paid\":true,"
        "\"public\":false,"
        "\"verified\":true"
        "}]}";

    const auto out_of_range = eth::rpc::load_rpc_manager_config_result_from_json_text( out_of_range_json );

    ASSERT_FALSE( out_of_range );
    EXPECT_EQ( out_of_range.error().code, rlp::base::json::JsonErrorCode::kOutOfRange );
    EXPECT_EQ( out_of_range.error().field, "endpoints[0].priority" );
}

TEST(RpcManagerConfigTest, AppliesSchemaDefaultsForOptionalFields)
{
    const std::string json_text =
        "{"
        "\"maxEndpointsPerChain\":1,"
        "\"endpoints\":[{"
        "\"chainName\":\"ethereum-mainnet\","
        "\"chainId\":1,"
        "\"urlTemplate\":\"https://eth.example\","
        "\"priority\":10,"
        "\"weight\":5,"
        "\"rateLimitPerSecond\":20"
        "}]"
        "}";

    const auto config = eth::rpc::load_rpc_manager_config_result_from_json_text( json_text );

    ASSERT_TRUE( config );
    ASSERT_EQ( config.value().endpoints.size(), 1U );
    EXPECT_FALSE( config.value().endpoints[0].api_key_env_var.has_value() );
    EXPECT_FALSE( config.value().endpoints[0].api_key_literal.has_value() );
    EXPECT_FALSE( config.value().endpoints[0].is_paid );
    EXPECT_TRUE( config.value().endpoints[0].is_public );
    EXPECT_FALSE( config.value().endpoints[0].verified );
}
