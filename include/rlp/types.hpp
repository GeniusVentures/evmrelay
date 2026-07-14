#ifndef RLP_TYPES_HPP
#define RLP_TYPES_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace rlp {

// Basic byte container types.
// Silence -Wdeprecated-declarations: std::char_traits<unsigned char> is deprecated in
// Xcode 26.5 / libc++ (char_traits<T> for T != char/wchar_t/char8_t/char16_t/char32_t
// is non-standard).  We intentionally use it because uint8_t is unsigned char on every
// platform we target and the deprecation removal timeframe is years away.  The pragma
// is scoped to these two using-declarations so callers are not affected.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
using Bytes = std::basic_string<uint8_t>;
using ByteView = std::basic_string_view<uint8_t>;
#pragma clang diagnostic pop

} // namespace rlp

#endif // RLP_TYPES_HPP
