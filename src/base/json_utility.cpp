// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <base/json_utility.hpp>

#include <limits>
#include <utility>

namespace rlp::base::json
{

namespace {

template <typename T>
[[nodiscard]] JsonResult<T> parse_unsigned_integer(const boost::json::value& value) noexcept
{
    if ( !value.is_int64() || value.as_int64() < 0 )
    {
        return outcome::failure( JsonError{JsonErrorCode::kWrongType, {}} );
    }

    const auto parsed = static_cast<uint64_t>( value.as_int64() );
    if ( parsed > static_cast<uint64_t>( std::numeric_limits<T>::max() ) )
    {
        return outcome::failure( JsonError{JsonErrorCode::kOutOfRange, {}} );
    }

    return static_cast<T>( parsed );
}

[[nodiscard]] JsonError with_field(JsonError error, std::string_view field_name)
{
    error.field = std::string( field_name );
    return error;
}

[[nodiscard]] std::string join_path(std::string_view prefix, std::string_view field_name)
{
    if ( prefix.empty() )
    {
        return std::string( field_name );
    }
    std::string path( prefix );
    path.push_back( '.' );
    path.append( field_name );
    return path;
}

[[nodiscard]] std::string array_path(std::string_view prefix, size_t index)
{
    std::string path( prefix );
    path.push_back( '[' );
    path.append( std::to_string( index ) );
    path.push_back( ']' );
    return path;
}

[[nodiscard]] JsonResult<JsonParsedObject> parse_schema_object_at_path(
    const boost::json::object& object,
    const JsonSchemaObject&    schema,
    std::string_view           path_prefix);

[[nodiscard]] JsonResult<JsonParsedArray> parse_schema_array_at_path(
    const boost::json::array& array,
    const JsonSchemaArray&    schema,
    std::string_view          path_prefix);

[[nodiscard]] JsonResult<JsonParsedValue> parse_schema_value_at_path(
    const boost::json::value& value,
    JsonFieldType             type,
    const JsonSchemaObject*   object_schema,
    const JsonSchemaArray*    array_schema,
    std::string_view          field_path)
{
    switch ( type )
    {
        case JsonFieldType::kString:
        {
            const auto parsed = parse_string( value, field_path );
            if ( !parsed )
            {
                return outcome::failure( parsed.error() );
            }
            return JsonParsedValue{type, parsed.value()};
        }
        case JsonFieldType::kBool:
        {
            const auto parsed = parse_bool( value, field_path );
            if ( !parsed )
            {
                return outcome::failure( parsed.error() );
            }
            return JsonParsedValue{type, parsed.value()};
        }
        case JsonFieldType::kU8:
        {
            const auto parsed = parse_u8( value, field_path );
            if ( !parsed )
            {
                return outcome::failure( parsed.error() );
            }
            return JsonParsedValue{type, parsed.value()};
        }
        case JsonFieldType::kU32:
        {
            const auto parsed = parse_u32( value, field_path );
            if ( !parsed )
            {
                return outcome::failure( parsed.error() );
            }
            return JsonParsedValue{type, parsed.value()};
        }
        case JsonFieldType::kU64:
        {
            const auto parsed = parse_u64( value, field_path );
            if ( !parsed )
            {
                return outcome::failure( parsed.error() );
            }
            return JsonParsedValue{type, parsed.value()};
        }
        case JsonFieldType::kSize:
        {
            const auto parsed = parse_size_t( value );
            if ( !parsed )
            {
                return outcome::failure( with_field( parsed.error(), field_path ) );
            }
            return JsonParsedValue{type, parsed.value()};
        }
        case JsonFieldType::kObject:
        {
            if ( object_schema == nullptr || !value.is_object() )
            {
                return outcome::failure( JsonError{JsonErrorCode::kWrongType, std::string( field_path )} );
            }
            const auto parsed = parse_schema_object_at_path( value.as_object(), *object_schema, field_path );
            if ( !parsed )
            {
                return outcome::failure( parsed.error() );
            }
            return JsonParsedValue{type, std::make_shared<JsonParsedObject>( parsed.value() )};
        }
        case JsonFieldType::kArray:
        {
            if ( array_schema == nullptr || !value.is_array() )
            {
                return outcome::failure( JsonError{JsonErrorCode::kWrongType, std::string( field_path )} );
            }
            const auto parsed = parse_schema_array_at_path( value.as_array(), *array_schema, field_path );
            if ( !parsed )
            {
                return outcome::failure( parsed.error() );
            }
            return JsonParsedValue{type, std::make_shared<JsonParsedArray>( parsed.value() )};
        }
    }
    return outcome::failure( JsonError{JsonErrorCode::kWrongType, std::string( field_path )} );
}

[[nodiscard]] JsonResult<JsonParsedArray> parse_schema_array_at_path(
    const boost::json::array& array,
    const JsonSchemaArray&    schema,
    std::string_view          path_prefix)
{
    JsonParsedArray parsed;
    parsed.values.reserve( array.size() );

    for ( size_t index = 0; index < array.size(); ++index )
    {
        const auto item_path = array_path( path_prefix, index );
        auto item = parse_schema_value_at_path(
            array[index],
            schema.element_type,
            schema.object_schema,
            schema.array_schema,
            item_path );
        if ( !item )
        {
            return outcome::failure( item.error() );
        }
        parsed.values.push_back( std::move( item.value() ) );
    }

    return parsed;
}

[[nodiscard]] JsonResult<JsonParsedObject> parse_schema_object_at_path(
    const boost::json::object& object,
    const JsonSchemaObject&    schema,
    std::string_view           path_prefix)
{
    JsonParsedObject parsed;
    parsed.fields.reserve( schema.fields.size() );

    for ( const auto& field : schema.fields )
    {
        const auto field_path = join_path( path_prefix, field.name );
        const auto* value = object.if_contains( std::string( field.name ) );
        if ( value == nullptr )
        {
            if ( field.default_value.has_value() )
            {
                auto default_value = parse_schema_value_at_path(
                    *field.default_value,
                    field.type,
                    field.object_schema,
                    field.array_schema,
                    field_path );
                if ( !default_value )
                {
                    return outcome::failure( default_value.error() );
                }
                parsed.fields.emplace( std::string( field.name ), std::move( default_value.value() ) );
                continue;
            }

            if ( field.required )
            {
                return outcome::failure( JsonError{JsonErrorCode::kMissingField, field_path} );
            }
            continue;
        }

        auto parsed_value = parse_schema_value_at_path(
            *value,
            field.type,
            field.object_schema,
            field.array_schema,
            field_path );
        if ( !parsed_value )
        {
            return outcome::failure( parsed_value.error() );
        }
        parsed.fields.emplace( std::string( field.name ), std::move( parsed_value.value() ) );
    }

    return parsed;
}

template <typename T>
[[nodiscard]] JsonResult<T> get_parsed_as(
    const JsonParsedObject& object,
    std::string_view        field_name,
    JsonFieldType           expected_type)
{
    const auto value = get_parsed_value( object, field_name );
    if ( !value )
    {
        return outcome::failure( value.error() );
    }
    if ( value.value()->type != expected_type )
    {
        return outcome::failure( JsonError{JsonErrorCode::kWrongType, std::string( field_name )} );
    }
    const auto* parsed = std::get_if<T>( &value.value()->value );
    if ( parsed == nullptr )
    {
        return outcome::failure( JsonError{JsonErrorCode::kWrongType, std::string( field_name )} );
    }
    return *parsed;
}

} // namespace

const char* to_string(JsonErrorCode code) noexcept
{
    switch ( code )
    {
        case JsonErrorCode::kParseFailed: return "parse failed";
        case JsonErrorCode::kRootNotObject: return "root is not an object";
        case JsonErrorCode::kMissingField: return "missing field";
        case JsonErrorCode::kWrongType: return "wrong type";
        case JsonErrorCode::kOutOfRange: return "out of range";
        case JsonErrorCode::kFileOpenFailed: return "file open failed";
    }
    return "unknown JSON error";
}

JsonResult<boost::json::object> parse_object(std::string_view json_text)
{
    boost::system::error_code ec;
    const auto parsed = boost::json::parse( json_text, ec );
    if ( ec )
    {
        return outcome::failure( JsonError{JsonErrorCode::kParseFailed, {}} );
    }

    const auto* object = parsed.if_object();
    if ( object == nullptr )
    {
        return outcome::failure( JsonError{JsonErrorCode::kRootNotObject, {}} );
    }

    return *object;
}

JsonResult<const boost::json::value*> get_value(
    const boost::json::object& object,
    std::string_view           field_name)
{
    const auto* value = object.if_contains( std::string( field_name ) );
    if ( value == nullptr )
    {
        return outcome::failure( JsonError{JsonErrorCode::kMissingField, std::string( field_name )} );
    }
    return value;
}

JsonResult<std::string> get_string(
    const boost::json::object& object,
    std::string_view           field_name)
{
    const auto value = get_value( object, field_name );
    if ( !value )
    {
        return outcome::failure( value.error() );
    }
    return parse_string( *value.value(), field_name );
}

JsonResult<std::string> parse_string(const boost::json::value& value) noexcept
{
    if ( !value.is_string() )
    {
        return outcome::failure( JsonError{JsonErrorCode::kWrongType, {}} );
    }

    const auto& string_value = value.as_string();
    return std::string( string_value.data(), string_value.size() );
}

JsonResult<std::string> parse_string(
    const boost::json::value& value,
    std::string_view          field_name) noexcept
{
    const auto parsed = parse_string( value );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

JsonResult<std::string> get_optional_string(
    const boost::json::object& object,
    std::string_view           field_name,
    std::string                default_value)
{
    const auto* value = object.if_contains( std::string( field_name ) );
    if ( value == nullptr )
    {
        return default_value;
    }
    if ( !value->is_string() )
    {
        return outcome::failure( JsonError{JsonErrorCode::kWrongType, std::string( field_name )} );
    }

    const auto& string_value = value->as_string();
    return std::string( string_value.data(), string_value.size() );
}

JsonResult<bool> get_bool(
    const boost::json::object& object,
    std::string_view           field_name)
{
    const auto value = get_value( object, field_name );
    if ( !value )
    {
        return outcome::failure( value.error() );
    }
    return parse_bool( *value.value(), field_name );
}

JsonResult<bool> parse_bool(const boost::json::value& value) noexcept
{
    if ( !value.is_bool() )
    {
        return outcome::failure( JsonError{JsonErrorCode::kWrongType, {}} );
    }
    return value.as_bool();
}

JsonResult<bool> parse_bool(
    const boost::json::value& value,
    std::string_view          field_name) noexcept
{
    const auto parsed = parse_bool( value );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

JsonResult<const boost::json::array*> get_array(
    const boost::json::object& object,
    std::string_view           field_name)
{
    const auto value = get_value( object, field_name );
    if ( !value )
    {
        return outcome::failure( value.error() );
    }
    const auto* array = value.value()->if_array();
    if ( array == nullptr )
    {
        return outcome::failure( JsonError{JsonErrorCode::kWrongType, std::string( field_name )} );
    }
    return array;
}

JsonResult<uint8_t> get_u8(
    const boost::json::object& object,
    std::string_view           field_name)
{
    const auto value = get_value( object, field_name );
    if ( !value )
    {
        return outcome::failure( value.error() );
    }
    auto parsed = parse_u8( *value.value() );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

JsonResult<uint32_t> get_u32(
    const boost::json::object& object,
    std::string_view           field_name)
{
    const auto value = get_value( object, field_name );
    if ( !value )
    {
        return outcome::failure( value.error() );
    }
    auto parsed = parse_u32( *value.value() );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

JsonResult<uint64_t> get_u64(
    const boost::json::object& object,
    std::string_view           field_name)
{
    const auto value = get_value( object, field_name );
    if ( !value )
    {
        return outcome::failure( value.error() );
    }
    auto parsed = parse_u64( *value.value() );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

JsonResult<size_t> parse_size_t(const boost::json::value& value) noexcept
{
    return parse_unsigned_integer<size_t>( value );
}

JsonResult<uint8_t> parse_u8(const boost::json::value& value) noexcept
{
    return parse_unsigned_integer<uint8_t>( value );
}

JsonResult<uint8_t> parse_u8(
    const boost::json::value& value,
    std::string_view          field_name) noexcept
{
    const auto parsed = parse_u8( value );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

JsonResult<uint32_t> parse_u32(const boost::json::value& value) noexcept
{
    return parse_unsigned_integer<uint32_t>( value );
}

JsonResult<uint32_t> parse_u32(
    const boost::json::value& value,
    std::string_view          field_name) noexcept
{
    const auto parsed = parse_u32( value );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

JsonResult<uint64_t> parse_u64(const boost::json::value& value) noexcept
{
    return parse_unsigned_integer<uint64_t>( value );
}

JsonResult<uint64_t> parse_u64(
    const boost::json::value& value,
    std::string_view          field_name) noexcept
{
    const auto parsed = parse_u64( value );
    if ( !parsed )
    {
        return outcome::failure( with_field( parsed.error(), field_name ) );
    }
    return parsed.value();
}

const JsonParsedValue* JsonParsedObject::find(std::string_view field_name) const
{
    const auto found = fields.find( std::string( field_name ) );
    if ( found == fields.end() )
    {
        return nullptr;
    }
    return &found->second;
}

JsonResult<JsonParsedObject> parse_schema_object(
    std::string_view        json_text,
    const JsonSchemaObject& schema)
{
    const auto root = parse_object( json_text );
    if ( !root )
    {
        return outcome::failure( root.error() );
    }
    return parse_schema_object_at_path( root.value(), schema, {} );
}

JsonResult<JsonParsedObject> parse_schema_object(
    const boost::json::object& object,
    const JsonSchemaObject&    schema)
{
    return parse_schema_object_at_path( object, schema, {} );
}

JsonResult<JsonParsedValue> parse_schema_value(
    const boost::json::value& value,
    JsonFieldType             type,
    const JsonSchemaObject*   object_schema,
    const JsonSchemaArray*    array_schema,
    std::string_view          field_path)
{
    return parse_schema_value_at_path( value, type, object_schema, array_schema, field_path );
}

JsonResult<const JsonParsedValue*> get_parsed_value(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    const auto* value = object.find( field_name );
    if ( value == nullptr )
    {
        return outcome::failure( JsonError{JsonErrorCode::kMissingField, std::string( field_name )} );
    }
    return value;
}

JsonResult<std::string> get_parsed_string(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    return get_parsed_as<std::string>( object, field_name, JsonFieldType::kString );
}

JsonResult<bool> get_parsed_bool(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    return get_parsed_as<bool>( object, field_name, JsonFieldType::kBool );
}

JsonResult<uint8_t> get_parsed_u8(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    return get_parsed_as<uint8_t>( object, field_name, JsonFieldType::kU8 );
}

JsonResult<uint32_t> get_parsed_u32(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    return get_parsed_as<uint32_t>( object, field_name, JsonFieldType::kU32 );
}

JsonResult<uint64_t> get_parsed_u64(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    return get_parsed_as<uint64_t>( object, field_name, JsonFieldType::kU64 );
}

JsonResult<size_t> get_parsed_size(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    return get_parsed_as<size_t>( object, field_name, JsonFieldType::kSize );
}

JsonResult<const JsonParsedArray*> get_parsed_array(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    const auto array = get_parsed_as<JsonParsedArrayPtr>( object, field_name, JsonFieldType::kArray );
    if ( !array )
    {
        return outcome::failure( array.error() );
    }
    return array.value().get();
}

JsonResult<const JsonParsedObject*> get_parsed_object(
    const JsonParsedObject& object,
    std::string_view        field_name)
{
    const auto parsed_object = get_parsed_as<JsonParsedObjectPtr>( object, field_name, JsonFieldType::kObject );
    if ( !parsed_object )
    {
        return outcome::failure( parsed_object.error() );
    }
    return parsed_object.value().get();
}

} // namespace rlp::base::json
