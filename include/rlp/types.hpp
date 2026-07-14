#ifndef RLP_TYPES_HPP
#define RLP_TYPES_HPP

#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <string>
#include <string_view>

namespace rlp {

/// @brief Custom char_traits for uint8_t — replaces the deprecated
///        std::char_traits<unsigned char> (Xcode 26.5 / libc++ deprecation).
///
/// Delegates to std::char_traits<char> via reinterpret_cast, which is safe
/// because uint8_t is unsigned char on all target platforms, and char_traits
/// operations are purely byte-level (no encoding awareness).
struct byte_traits
{
    using char_type  = uint8_t;
    using int_type   = unsigned int;
    using off_type   = std::streamoff;
    using pos_type   = std::streampos;
    using state_type = std::mbstate_t;

    static void assign( char_type &c1, const char_type &c2 ) noexcept { c1 = c2; }
    static constexpr bool eq( char_type c1, char_type c2 ) noexcept { return c1 == c2; }
    static constexpr bool lt( char_type c1, char_type c2 ) noexcept { return c1 < c2; }

    static int compare( const char_type *s1, const char_type *s2, size_t n )
    {
        return std::memcmp( s1, s2, n );
    }

    static size_t length( const char_type *s )
    {
        return std::strlen( reinterpret_cast<const char *>( s ) );
    }

    static const char_type *find( const char_type *s, size_t n, const char_type &a )
    {
        return static_cast<const char_type *>( std::memchr( s, a, n ) );
    }

    static char_type *move( char_type *s1, const char_type *s2, size_t n )
    {
        return static_cast<char_type *>( std::memmove( s1, s2, n ) );
    }

    static char_type *copy( char_type *s1, const char_type *s2, size_t n )
    {
        return static_cast<char_type *>( std::memcpy( s1, s2, n ) );
    }

    static char_type *assign( char_type *s, size_t n, char_type a )
    {
        return static_cast<char_type *>( std::memset( s, a, n ) );
    }

    static constexpr int_type not_eof( int_type c ) noexcept
    {
        return ( c == eof() ) ? static_cast<int_type>( ~eof() ) : c;
    }

    static constexpr char_type to_char_type( int_type c ) noexcept
    {
        return static_cast<char_type>( c );
    }

    static constexpr int_type to_int_type( char_type c ) noexcept
    {
        return static_cast<int_type>( c );
    }

    static constexpr bool eq_int_type( int_type c1, int_type c2 ) noexcept
    {
        return c1 == c2;
    }

    static constexpr int_type eof() noexcept { return static_cast<int_type>( ~0u ); }
};

// Basic byte container types — use custom byte_traits so we never
// instantiate the deprecated std::char_traits<unsigned char>.
using Bytes    = std::basic_string<uint8_t, byte_traits>;
using ByteView = std::basic_string_view<uint8_t, byte_traits>;

} // namespace rlp

#endif // RLP_TYPES_HPP
