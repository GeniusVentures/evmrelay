# Coding Conventions

**Analysis Date:** 2026-05-25

## Naming Patterns

**Namespaces:**
- All lowercase, single word or snake_case. Nested with `::`.
  - `rlp`, `eth`, `rlpx`, `discv4`, `discv5`, `eth::abi`, `eth::rpc`, `eth::protocol`, `rlp::base::json`, `rlpx::crypto`, `discv4`

**Classes:**
- PascalCase. Examples: `RlpEncoder`, `RlpDecoder`, `EthWatchService`, `RpcHttpTransport`, `PropertyBasedTest`

**Methods:**
- **Public API methods:** Mixed. The RLP core library uses PascalCase for public methods (`GetBytes()`, `MoveBytes()`, `BeginList()`, `EndList()`, `AddRaw()`, `PeekHeader()`, `SkipItem()`, `ReadListHeaderBytes()`, `PeekPayload()`), while the ETH layer and newer code tends toward camelCase for simpler APIs (`read()`, `add()`, `clear()`, `set_send_callback()`, `process_message()`, `watch_event()`, `unwatch()`). Private/internal methods are consistently camelCase (`encode_header_bytes()`, `decode_header_internal()`, `read_integral()`, `read_uint256()`, `skip_header_internal()`).
- Follow the existing convention in the file/class you are modifying.

**Variables:**
- camelCase. Member variables use trailing underscore: `buffer_`, `view_`, `list_start_positions_`, `encoder_`, `payload_size_`, `source_`, `service_`. Local variables use plain camelCase: `payload_len`, `start_pos`, `header_bytes`.

**Constants:**
- `inline constexpr` with `kCamelCase` prefix. Examples: `kEmptyStringCode`, `kMaxShortStringLen`, `kLongPrefixByteSize`, `kRlpSingleByteThreshold`, `kSingleByteStringSize`, `kLongPrefixByteSize`, `kEthProtocolVersion68`, `kStatusHandshakeTimeout`, `kGetReceiptsMessageId`.
- Never use `#define` for value constants.

**Enums:**
- `enum class` with `kCamelCase` values. Examples:
  - `EncodingError::kPayloadTooLarge`, `DecodingError::kOverflow`, `CryptoError::kKdfFailed`
  - `RpcBlockTag::kLatest`, `EthWatchDiscoveryMode::kHybrid`
  - `Leftover::kProhibit`, `AbiParamKind::kAddress`

**Type Aliases:**
- PascalCase. Examples: `Bytes`, `ByteView`, `EncodingResult`, `EncodingOperationResult`, `Result`, `DecodingResult`, `Hash256`, `Address`, `Bloom`, `ForkId`, `EventWatchId`. `using` declarations preferred over `typedef`.

**Files:**
- Headers: `snake_case.hpp` — `rlp_encoder.hpp`, `json_rpc.hpp`, `eth_types.hpp`, `eth_watch_service.hpp`
- Source: `snake_case.cpp` — `rlp_encoder.cpp`, `rlp_decoder.cpp`, `json_rpc.cpp`, `rpc_http_transport.cpp`
- Test files: `snake_case_test.cpp` — `rlp_encoder_tests.cpp`, `abi_decoder_test.cpp`, `eth_watch_mock_peer_test.cpp`

## Code Style

**Formatting:**
- Tool: `clang-format` (config at `evmrelay/.clang-format`)
- Based on: Microsoft style with extensive customizations
- Indent: 4 spaces
- Line length: 120 characters maximum
- Brace style: Allman/Ullman — braces on their own line (enforced by `InsertBraces: true` and `BraceWrapping` rules)
- Spaces inside parentheses: Custom — space after `(` and before `)` in conditionals and all other contexts (`if ( condition )`)
- Template declarations: Always broken onto separate lines
- Constructor initializers: Break after colon, pack on next line
- Namespace indentation: All namespaces indented
- Trailing commas: Added when wrapped

**Linting:**
- Tool: `clang-tidy` (config at `evmrelay/.clang-tidy`)
- Enabled checks: `boost-*`, `bugprone-*` (with some exceptions), `cert-*` (with exceptions), `concurrency-*`, `cppcoreguidelines-*` (with exceptions), `google-*` (select rules), `misc-*`, `modernize-*` (with exceptions), `performance-*`, `portability-*`, `readability-*` (with exceptions)
- Disabled modernize checks: `-modernize-use-default-member-init`, `-modernize-use-trailing-return-type`
- Naming conventions enforced:
  - Class member prefix: `m_` (clang-tidy, though actual code uses trailing `_`)
  - Enum values: `CamelCase`
  - Enum constants: `UPPER_CASE` (clang-tidy, though code uses `kCamelCase`)
  - Type aliases/typedefs: `CamelCase`
- Format style: `file` (reads `.clang-format`)

**Compiler:**
- C++ standard: C++17 (`CMAKE_CXX_STANDARD 17`, `Standard: c++17` in `.clang-format`)
- No C++20 features (designated initializers forbidden)
- Compiler extensions disabled (`CMAKE_CXX_EXTENSIONS OFF`)

## Copyright Headers

Every header and source file starts with:
```cpp
// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
```

## Import Organization

**Include path style:**
- RLP core: `#include <rlp/rlp_encoder.hpp>` — direct include using the public include directory
- ETH layer: `#include <eth/json_rpc.hpp>`
- RLPx: `#include <rlpx/crypto/kdf.hpp>`
- Base utilities: `#include <base/json_utility.hpp>`
- Internal implementation files may use quoted includes: `#include "rlp/common.hpp"` (seen in `common.cpp`)

**Include order (observed pattern):**
1. Primary public header for the .cpp file
2. Project headers (alphabetical or grouped by module)
3. Standard library headers
4. Third-party headers (boost, openssl, secp256k1)

**Path aliases:**
- No explicit path aliases. Public include directory is `evmrelay/include/`, so `#include <rlp/...>` maps to `evmrelay/include/rlp/...`

## Error Handling

**Framework:** `boost::outcome` (Boost.Outcome) — `BOOST_OUTCOME_TRY` macro for propagation.

**Pattern:**
- Each module defines its own error enum and result type aliases
- `evmrelay/include/rlp/result.hpp`:
  ```cpp
  template <class T>
  using EncodingResult = outcome::result<T, EncodingError, outcome::policy::all_narrow>;
  using EncodingOperationResult = outcome::result<void, EncodingError, outcome::policy::all_narrow>;
  ```
- `evmrelay/include/rlpx/rlpx_error.hpp`: `Result<T>`, `AuthResult<T>`, `FramingResult<T>`, `CryptoResult<T>`
- `evmrelay/include/discv4/discv4_error.hpp`: `Result<T>`, `VoidResult`
- `evmrelay/include/eth/messages.hpp`: `EncodeResult`, `DecodeResult<T>`, `ValidationResult`
- `evmrelay/include/base/json_utility.hpp`: `JsonResult<T>` with custom `JsonError` struct

**Error propagation:**
```cpp
BOOST_OUTCOME_TRY(auto header, encode_header_bytes(false, bytes.length()));
```
On failure, the error is returned from the current function.

**Success returns:**
```cpp
return outcome::success();
```

**Error checking in callers:**
```cpp
ASSERT_TRUE(result);               // in tests
if (!result) { return result.error(); }
```

**Error stringification:** Each module provides a `to_string()` or `*_error_to_string()` function:
- `evmrelay/src/rlp/common.cpp`: `encoding_error_to_string()`, `decoding_error_to_string()`, `streaming_error_to_string()`
- `rlpx::to_string(SessionError)`, `discv4::to_string(discv4Error)`

## Logging

**Framework:** `spdlog` (via `spdlog/spdlog.h`).

**Patterns:**
- Logger type: `std::shared_ptr<spdlog::logger>` aliased as `rlp::base::Logger`
- Factory function: `rlp::base::createLogger(tag, basepath)` in `evmrelay/include/base/rlp-logger.hpp`
- Android support: `spdlog/sinks/android_sink.h` when `ANDROID` is defined
- Logger is obtained/created at module initialization, passed where needed

**Logging in application code:**
- Use `spdlog` macros: `SPDLOG_INFO(...)`, `SPDLOG_ERROR(...)`, etc. (via the logger object)
- Do not use `std::cout`/`printf` for production logging
- Test code may use `std::cout` for diagnostic output (e.g., random seed printing in property tests)

## Comments

**Doxygen-style preferred:**
- Public API functions: `/// @brief` single-line or `/** @brief */` multi-line
- Parameters: `@param name Description`
- Return: `@return Description`
- File-level: `/** @file filename.hpp @brief Description */`

**Examples from codebase:**
```cpp
/**
 * @brief Create a logger instance.
 * @param tag Tagging name for identifying logger.
 * @param basepath Optional base path for log output (platform dependent).
 * @return Logger object.
 */
Logger createLogger( const std::string &tag, const std::string &basepath = "" );
```

```cpp
// Encodes just the header bytes into a temporary buffer
EncodingResult<Bytes> encode_header_bytes(bool list, size_t payload_size_bytes) noexcept {
```

**When to comment:** Public interfaces require Doxygen. Internal helpers use inline `//` comments for non-obvious logic. Do NOT leave TODO comments as architecture.

## Function Design

**`noexcept`:** Every function is declared `noexcept` unless it explicitly needs to throw (which is rare). This is a hard rule across the entire codebase.

**`[[nodiscard]]`:** Applied to any function returning a value where ignoring the return is a likely error. Seen on all result-returning functions, size queries, factory functions.

**`const` correctness:**
- Member functions that don't modify state are `const`
- Parameters passed by `const&` or `const T*`
- Variables that don't change are `const`

**Parameter passing:**
- Small/built-in types: by value
- Larger types (strings, vectors, structs): by `const&`
- Output parameters: by non-const reference (`out` suffix convention: `uint64_t& out`)

**Return values:**
- Result types (`Result<T>`) for fallible operations
- `std::optional<T>` for queries that may not have an answer
- Raw values for infallible operations

**Template design:**
- SFINAE with `std::enable_if_t` and type traits (`is_unsigned_integral_v`, `is_rlp_decodable_v`)
- Template implementations in headers (no `.ipp` files observed, but noted as possible in comments)
- `if constexpr` for compile-time branching in templates

**Size:** Functions tend to be focused and short (typically <50 lines), though some template specializations in headers are longer due to compile-time branches.

## Module Design

**Exports:** Public headers in `evmrelay/include/<module>/` expose the module's API. Implementation details in `evmrelay/src/<module>/`. No `detail/` or `internal/` namespaces observed barring some `impl/` directories in the parent super-project.

**Barrel files:** Some modules have aggregation headers:
- `evmrelay/include/rlp/common.hpp` — includes `types.hpp`, `constants.hpp`, `errors.hpp`, `result.hpp`, `traits.hpp`
- `evmrelay/include/rlp/rlp_ethereum.hpp` — provides Ethereum-specific free functions

**Namespace design:**
- Each module owns a top-level namespace (`rlp`, `eth`, `rlpx`, `discv4`, `discv5`)
- Sub-modules use nested namespaces (`eth::abi`, `eth::rpc`, `eth::protocol`, `rlp::base::json`, `rlpx::crypto`)
- Anonymous namespaces (`namespace { ... }`) for file-local helpers in `.cpp` files

**Free functions preferred:** The codebase follows "prefer free functions" design. Examples:
- `rlp::addAddress(encoder, addr)` rather than `encoder.addAddress(addr)`
- `eth::abi::keccak256(data, len)` rather than a hasher object method
- `rlp::base::byte_encoding::append_u32_be(out, value)` — utility free functions

## Configuration & Data-Driven Design

Per `AgentDocs/CLAUDE.md`:
- Never hard-code chain names, network IDs, ports, fork hashes, bootnodes, RPC URLs, etc. in C++ source
- Values that vary by chain, network, or deployment belong in data/configuration
- Use JSON schemas, parsers, and validators (see `evmrelay/include/base/json_utility.hpp` for schema-based JSON parsing)
- Tests may use fixtures but must not become production registries

---

*Convention analysis: 2026-05-25*
