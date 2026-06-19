// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_BASE_JSON_UTILITY_HPP
#define EVMRELAY_INCLUDE_BASE_JSON_UTILITY_HPP

#include <boost/json.hpp>
#include <boost/outcome/result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace rlp::base::json
{

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

enum class JsonErrorCode
{
    kParseFailed,
    kRootNotObject,
    kMissingField,
    kWrongType,
    kOutOfRange,
    kFileOpenFailed
};

struct JsonError
{
    JsonErrorCode code = JsonErrorCode::kParseFailed;
    std::string   field;
};

template <typename T>
using JsonResult = outcome::result<T, JsonError, outcome::policy::all_narrow>;

[[nodiscard]] const char* to_string(JsonErrorCode code) noexcept;

[[nodiscard]] JsonResult<boost::json::object> parse_object(std::string_view json_text);
[[nodiscard]] JsonResult<const boost::json::value*> get_value(
    const boost::json::object& object,
    std::string_view           field_name);
[[nodiscard]] JsonResult<std::string> get_string(
    const boost::json::object& object,
    std::string_view           field_name);
[[nodiscard]] JsonResult<std::string> parse_string(
    const boost::json::value& value) noexcept;
[[nodiscard]] JsonResult<std::string> parse_string(
    const boost::json::value& value,
    std::string_view           field_name) noexcept;
[[nodiscard]] JsonResult<std::string> get_optional_string(
    const boost::json::object& object,
    std::string_view           field_name,
    std::string                default_value);
[[nodiscard]] JsonResult<bool> get_bool(
    const boost::json::object& object,
    std::string_view           field_name);
[[nodiscard]] JsonResult<bool> parse_bool(
    const boost::json::value& value) noexcept;
[[nodiscard]] JsonResult<bool> parse_bool(
    const boost::json::value& value,
    std::string_view           field_name) noexcept;
[[nodiscard]] JsonResult<const boost::json::array*> get_array(
    const boost::json::object& object,
    std::string_view           field_name);
[[nodiscard]] JsonResult<uint8_t> get_u8(
    const boost::json::object& object,
    std::string_view           field_name);
[[nodiscard]] JsonResult<uint32_t> get_u32(
    const boost::json::object& object,
    std::string_view           field_name);
[[nodiscard]] JsonResult<uint64_t> get_u64(
    const boost::json::object& object,
    std::string_view           field_name);

[[nodiscard]] JsonResult<size_t> parse_size_t(const boost::json::value& value) noexcept;
[[nodiscard]] JsonResult<uint8_t> parse_u8(const boost::json::value& value) noexcept;
[[nodiscard]] JsonResult<uint8_t> parse_u8(
    const boost::json::value& value,
    std::string_view          field_name) noexcept;
[[nodiscard]] JsonResult<uint32_t> parse_u32(const boost::json::value& value) noexcept;
[[nodiscard]] JsonResult<uint32_t> parse_u32(
    const boost::json::value& value,
    std::string_view          field_name) noexcept;
[[nodiscard]] JsonResult<uint64_t> parse_u64(const boost::json::value& value) noexcept;
[[nodiscard]] JsonResult<uint64_t> parse_u64(
    const boost::json::value& value,
    std::string_view          field_name) noexcept;

enum class JsonFieldType
{
    kString,
    kBool,
    kU8,
    kU32,
    kU64,
    kSize,
    kObject,
    kArray
};

struct JsonSchemaObject;
struct JsonSchemaArray;

struct JsonSchemaField
{
    std::string_view        name;
    JsonFieldType           type = JsonFieldType::kString;
    bool                    required = true;
    std::optional<boost::json::value> default_value;
    const JsonSchemaObject* object_schema = nullptr;
    const JsonSchemaArray*  array_schema = nullptr;
};

struct JsonSchemaObject
{
    std::vector<JsonSchemaField> fields;
};

struct JsonSchemaArray
{
    JsonFieldType           element_type = JsonFieldType::kString;
    const JsonSchemaObject* object_schema = nullptr;
    const JsonSchemaArray*  array_schema = nullptr;
};

struct JsonParsedObject;
struct JsonParsedArray;

using JsonParsedObjectPtr = std::shared_ptr<JsonParsedObject>;
using JsonParsedArrayPtr = std::shared_ptr<JsonParsedArray>;

struct JsonParsedSize
{
    size_t value = 0;
};

struct JsonParsedValue
{
    using Storage = std::variant<
        std::string,
        bool,
        uint8_t,
        uint32_t,
        uint64_t,
        JsonParsedSize,
        JsonParsedObjectPtr,
        JsonParsedArrayPtr>;

    JsonFieldType type = JsonFieldType::kString;
    Storage       value;
};

struct JsonParsedObject
{
    std::unordered_map<std::string, JsonParsedValue> fields;

    [[nodiscard]] const JsonParsedValue* find(std::string_view field_name) const;
};

struct JsonParsedArray
{
    std::vector<JsonParsedValue> values;
};

[[nodiscard]] JsonResult<JsonParsedObject> parse_schema_object(
    std::string_view         json_text,
    const JsonSchemaObject&  schema);
[[nodiscard]] JsonResult<JsonParsedObject> parse_schema_object(
    const boost::json::object& object,
    const JsonSchemaObject&    schema);
[[nodiscard]] JsonResult<JsonParsedValue> parse_schema_value(
    const boost::json::value& value,
    JsonFieldType             type,
    const JsonSchemaObject*   object_schema,
    const JsonSchemaArray*    array_schema,
    std::string_view          field_path);

[[nodiscard]] JsonResult<const JsonParsedValue*> get_parsed_value(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<std::string> get_parsed_string(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<bool> get_parsed_bool(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<uint8_t> get_parsed_u8(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<uint32_t> get_parsed_u32(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<uint64_t> get_parsed_u64(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<size_t> get_parsed_size(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<const JsonParsedArray*> get_parsed_array(
    const JsonParsedObject& object,
    std::string_view        field_name);
[[nodiscard]] JsonResult<const JsonParsedObject*> get_parsed_object(
    const JsonParsedObject& object,
    std::string_view        field_name);

} // namespace rlp::base::json

#endif // EVMRELAY_INCLUDE_BASE_JSON_UTILITY_HPP
