# Testing Patterns

**Analysis Date:** 2026-05-25

## Test Framework

**Runner:**
- Google Test (gtest) — found via `find_package(GTest CONFIG REQUIRED)` in `evmrelay/CMakeLists.txt`
- Included with `#include <gtest/gtest.h>`

**Assertion Library:**
- Google Test built-in assertions (`ASSERT_*`, `EXPECT_*`)

**CMake Integration:**
- Custom `addtest()` function used to create test executables
- Tests link against the `evmrelay` static library target

**Run Commands:**
```bash
# Build and run (from build/<Platform>/<BuildType>/)
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
ninja

# Run individual test binary
./test/rlp/rlp_encoder_tests
./test/eth/abi_decoder_test

# Run all tests via CTest
ctest

# Coverage analysis (from project root)
./evmrelay/scripts/coverage_analysis.sh
```

## Test File Organization

**Location:**
- Separate test directory: `evmrelay/test/<module>/`
- Tests mirror the source module structure: `test/rlp/`, `test/eth/`, `test/rlpx/`, `test/discv4/`, `test/discv5/`, `test/fuzz/`

**Naming:**
- Test files: `snake_case_test.cpp` — e.g., `rlp_encoder_tests.cpp`, `rlp_decoder_tests.cpp`, `abi_decoder_test.cpp`, `eth_watch_service_test.cpp`
- Test helpers: `test_helpers.hpp` (e.g., `evmrelay/test/rlp/test_helpers.hpp`)
- Fuzz tests: `fuzz_rlp_decoder.cpp`, `fuzz_rlp_encoder.cpp`

**Structure:**
```
evmrelay/test/
├── CMakeLists.txt                 # Top-level test registration
├── rlp/
│   ├── CMakeLists.txt             # RLP test targets
│   ├── test_helpers.hpp           # Shared hex conversion utilities
│   ├── rlp_encoder_tests.cpp
│   ├── rlp_decoder_tests.cpp
│   ├── rlp_endian_tests.cpp
│   ├── rlp_edge_cases.cpp
│   ├── rlp_benchmark_tests.cpp
│   ├── rlp_property_tests.cpp
│   ├── rlp_comprehensive_tests.cpp
│   ├── rlp_ethereum_tests.cpp
│   ├── rlp_random_tests.cpp
│   ├── rlp_type_safety_tests.cpp
│   ├── rlp_enhanced_api_tests.cpp
│   ├── rlp_streaming_decoder_tests.cpp
│   ├── rlp_streaming_simple_api_demo.cpp
│   ├── rlp_ethereum_real_world_examples.cpp
│   └── rlp_profiling_tests.cpp
├── eth/
│   ├── CMakeLists.txt
│   ├── abi_decoder_test.cpp
│   ├── eth_messages_test.cpp
│   ├── eth_handshake_test.cpp
│   ├── eth_handshake_guard_test.cpp
│   ├── eth_objects_test.cpp
│   ├── eth_transactions_test.cpp
│   ├── eth_watch_integration_test.cpp
│   ├── eth_watch_mock_peer_test.cpp
│   ├── eth_watch_runner_test.cpp
│   ├── eth_watch_service_test.cpp
│   ├── eth_watch_cli_test.cpp
│   ├── eth_watch_all_chains_live_test.cpp
│   ├── eth_enr_tree_peer_cache_live_test.cpp
│   ├── eth_peer_session_test.cpp
│   ├── eth_receipt_source_test.cpp
│   ├── bridge_event_test.cpp
│   ├── bridge_observation_test.cpp
│   ├── event_filter_test.cpp
│   ├── finality_policy_test.cpp
│   ├── json_rpc_test.cpp
│   ├── rpc_manager_test.cpp
│   ├── rpc_manager_config_test.cpp
│   ├── rpc_http_transport_test.cpp
│   ├── rpc_receipt_source_test.cpp
│   ├── chain_tracker_test.cpp
│   ├── gnus_contracts_test.cpp
│   └── secp256k1_utility_test.cpp
├── rlpx/
│   ├── CMakeLists.txt
│   ├── crypto_test.cpp
│   ├── frame_cipher_test.cpp
│   ├── handshake_vectors_test.cpp
│   ├── message_routing_test.cpp
│   ├── protocol_messages_test.cpp
│   ├── rlpx_session_test.cpp
│   ├── rlpx_state_test.cpp
│   ├── snappy_test.cpp
│   ├── socket_lifecycle_test.cpp
│   └── capability_negotiation_test.cpp
├── discv4/
│   ├── CMakeLists.txt
│   ├── discovery_test.cpp
│   ├── discv4_client_test.cpp
│   ├── discv4_protocol_test.cpp
│   ├── chain_peers_test.cpp
│   ├── dial_filter_test.cpp
│   ├── dial_history_test.cpp
│   ├── dial_scheduler_test.cpp
│   ├── enr_client_test.cpp
│   ├── enr_enrichment_test.cpp
│   ├── enr_request_test.cpp
│   └── enr_response_test.cpp
├── discv5/
│   ├── CMakeLists.txt
│   ├── discv5_client_test.cpp
│   ├── discv5_crawler_test.cpp
│   ├── discv5_enr_test.cpp
│   └── enr_tree_test.cpp
└── fuzz/
    ├── CMakeLists.txt
    ├── fuzz_rlp_decoder.cpp
    └── fuzz_rlp_encoder.cpp
```

## Test Structure

**Suite Organization:**
```cpp
#include <gtest/gtest.h>
#include <rlp/rlp_encoder.hpp>
#include "test_helpers.hpp"

using namespace rlp;
using namespace rlp::test;

TEST(RlpEncoder, EncodeEmptyString) {
    rlp::RlpEncoder encoder;
    encoder.add(rlp::ByteView{});
    auto result = encoder.GetBytes();
    ASSERT_TRUE(result);
    EXPECT_EQ(to_hex(*result.value()), "80");
}
```

**Patterns:**
- `TEST(SuiteName, TestName)` for standalone tests
- `TEST_F(FixtureName, TestName)` for fixture-based tests
- Descriptive test names: `EncodeEmptyString`, `DecodeUintZero`, `WatchAndUnwatch`, `ProcessReceiptsDecodesTransferEvent`
- Arrange-Act-Assert pattern: create object → perform operation → verify result
- Consistent use of `ASSERT_TRUE(result)` before dereferencing `result.value()` on `outcome::result` types
- `EXPECT_TRUE(result)` for decoding operations where boolean success is the primary check
- `EXPECT_EQ(actual, expected)` with expected value first

**Fixture pattern:**
```cpp
class EthereumRlpTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

    template<typename T>
    void test_roundtrip(const T& value, const std::string& expected_hex = "") {
        RlpEncoder encoder;
        encoder.add(value);
        auto encoded_result = encoder.GetBytes();
        ASSERT_TRUE(encoded_result);
        // ... decode and verify
    }
};

TEST_F(EthereumRlpTest, OfficialStringTests) {
    test_roundtrip(Bytes{}, "80");
    // ...
}
```

**Property-based test pattern:**
```cpp
class PropertyBasedTest : public ::testing::Test {
protected:
    std::mt19937 rng_;

    template<typename TestFunc>
    void run_property_test(TestFunc&& test_func, int iterations = 1000) {
        for (int i = 0; i < iterations; ++i) {
            test_func(i);
        }
    }
};
```

## Mocking

**Framework:** No mocking framework. Manual test doubles used.

**Patterns:**
```cpp
// Callback-based stub for network operations
discv4::DialFn no_op_dial_fn()
{
    return [](
        discv4::ValidatedPeer,
        std::function<void(rlpx::DisconnectReason)> done,
        std::function<void(std::shared_ptr<rlpx::RlpxSession>)>,
        boost::asio::yield_context)
    {
        done(rlpx::DisconnectReason::kTcpError);
    };
}

// Capture callback for message sending
std::vector<uint8_t> get_receipts_payload;
watch_service.set_send_callback([&get_receipts_payload](uint8_t, std::vector<uint8_t> payload)
{
    get_receipts_payload = std::move(payload);
});

// Capture callback invocation count
int callback_count = 0;
watch_service.watch_event(token, "Transfer(address,address,uint256)", params,
    [&callback_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&)
    {
        ++callback_count;
    });
```

**What to Mock:**
- Network I/O (dial functions, send/receive callbacks)
- External services (RPC HTTP transport not instantiated in unit tests)

**What NOT to Mock:**
- Core RLP encoding/decoding — tested with real data
- ABI decoder — tested with crafted binary data
- Event filter matching — tested with full log entries

## Fixtures and Factories

**Test Data:**
```cpp
// Template factory for filled arrays
template <typename Array>
Array make_filled(uint8_t seed)
{
    Array value{};
    for (size_t i = 0; i < value.size(); ++i)
    {
        value[i] = static_cast<uint8_t>(seed + i);
    }
    return value;
}

// Factory for test addresses/hashes
const auto token = make_filled<eth::codec::Address>(0xAA);
const auto from  = make_filled<eth::codec::Address>(0x11);

// Factory for ABI words
eth::codec::Hash256 make_address_word(const eth::codec::Address& address)
{
    eth::codec::Hash256 word{};
    std::copy(address.begin(), address.end(), word.begin() + 12);
    return word;
}

// Factory for log entries
eth::codec::LogEntry make_transfer_log(
    const eth::codec::Address& token,
    const eth::codec::Address& from,
    const eth::codec::Address& to,
    uint64_t amount)
{
    eth::codec::LogEntry log;
    log.address = token;
    log.topics.push_back(eth::abi::event_signature_hash("Transfer(address,address,uint256)"));
    log.topics.push_back(make_address_word(from));
    log.topics.push_back(make_address_word(to));
    append_uint256(log.data, amount);
    return log;
}

// Hex string conversions for RLP tests
Bytes from_hex(std::string_view hex);    // "82abba" → Bytes{0x82, 0xab, 0xba}
std::string to_hex(ByteView bytes);      // Bytes{0x82, 0xab, 0xba} → "82abba"
```

**Location:**
- `evmrelay/test/rlp/test_helpers.hpp` — shared hex conversion utilities
- Anonymous namespaces within test files for module-specific factories
- Test fixture classes for shared setup (e.g., `PropertyBasedTest`, `EthereumRlpTest`)

## Coverage

**Requirements:** Target >95% line coverage.
- Enforced by `evmrelay/scripts/coverage_analysis.sh`
- Script exits with code 1 if coverage < 95%

**Tooling:**
- `gcov` + `lcov` for instrumentation and reporting
- `genhtml` for HTML report generation

**View Coverage:**
```bash
cd evmrelay/scripts
./coverage_analysis.sh
# Opens HTML report at coverage_report/index.html
```

**Build flags for coverage:**
```cmake
-DCMAKE_CXX_FLAGS="--coverage -g -O0 -fprofile-arcs -ftest-coverage"
-DCMAKE_C_FLAGS="--coverage -g -O0 -fprofile-arcs -ftest-coverage"
-DCMAKE_EXE_LINKER_FLAGS="--coverage"
```

## Test Types

**Unit Tests:**
- Test individual RLP encoding/decoding operations
- Test ABI decoder on crafted binary data
- Test crypto primitives (KDF, ECDH, HMAC, AES)
- Test protocol message encode/decode round-trips
- Test ETH handshake validation logic
- Test event filter matching
- Scope: Single class or function, no external dependencies
- Located in `test/rlp/`, `test/eth/`, `test/rlpx/`, `test/discv4/`, `test/discv5/`

**Property Tests:**
- Randomized round-trip testing (encode → decode → verify)
- Located in `test/rlp/rlp_property_tests.cpp`
- Uses `std::mt19937` with logged seed for reproducibility
- Example: encode a random uint256, decode it, verify equality — repeated 1000+ iterations

**Benchmark Tests:**
- Performance measurements for RLP operations
- Located in `test/rlp/rlp_benchmark_tests.cpp`

**Integration Tests:**
- End-to-end flow tests for the ETH watch pipeline
- `eth_watch_integration_test.cpp` — full watch flow with simulated peer
- `eth_watch_mock_peer_test.cpp` — simulated receipts flow
- Scope: Multiple components interacting without real network

**Live Tests:**
- Tests that require actual network connectivity
- `eth_watch_all_chains_live_test.cpp` — all-chain discovery + RLPx/ETH functional test
- `eth_enr_tree_peer_cache_live_test.cpp` — live ENR-tree discovery
- Not run by default CI; manual execution only

**Fuzz Tests:**
- libFuzzer integration for RLP decoder and encoder
- Located in `test/fuzz/fuzz_rlp_decoder.cpp`, `test/fuzz/fuzz_rlp_encoder.cpp`
- Requires Clang and `-DENABLE_FUZZING=ON`
- Sanitizers: `-fsanitize=fuzzer,address`
- Run manually with corpus directories

**Edge Case Tests:**
- Boundary conditions, overflow scenarios, canonical encoding
- Located in `test/rlp/rlp_edge_cases.cpp`, `test/rlp/rlp_type_safety_tests.cpp`

**Real-World Examples:**
- Tests using actual Ethereum test vectors
- Located in `test/rlp/rlp_ethereum_tests.cpp`, `test/rlp/rlp_ethereum_real_world_examples.cpp`

## Common Patterns

**Async Testing:**
- Not applicable — the codebase uses `boost::asio` coroutines (`yield_context`) for async I/O, but tests operate synchronously

**Error Testing:**
```cpp
// Test that an operation fails with the expected error
TEST(CryptoTest, KdfEmptySecret) {
    std::vector<uint8_t> empty_secret;
    auto result = Kdf::derive(empty_secret, 32);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), CryptoError::kKdfFailed);
}

// Test that decoder rejects malformed input
TEST(RlpDecoder, DecodeMalformedHeader) {
    rlp::Bytes data = from_hex("f8"); // Invalid: too long but no length bytes
    rlp::RlpDecoder decoder(data);
    uint64_t out;
    EXPECT_FALSE(decoder.read(out));
}

// Test that unclosed list returns error
// (tested implicitly via ASSERT_TRUE on valid rounds, EXPECT_FALSE on error cases)
```

**Round-Trip Pattern:**
```cpp
// The most common testing pattern — encode, decode, verify
TEST(RlpEncoder, EncodeUintRoundtrip) {
    rlp::RlpEncoder encoder;
    uint64_t original = 0xFFCCB5DDFFEE1483;
    encoder.add(original);
    auto encoded_result = encoder.GetBytes();
    ASSERT_TRUE(encoded_result);

    rlp::RlpDecoder decoder(*encoded_result.value());
    uint64_t decoded;
    ASSERT_TRUE(decoder.read(decoded));
    EXPECT_EQ(decoded, original);
    EXPECT_TRUE(decoder.IsFinished());
}
```

**Test CMakeLists pattern:**
```cmake
# Each test is a separate executable
addtest(rlp_encoder_tests
    rlp_encoder_tests.cpp
)
target_link_libraries(rlp_encoder_tests
    evmrelay
)

# Suppress nodiscard warnings in test code
target_compile_options(rlp_encoder_tests PRIVATE -Wno-unused-result)
```

---

*Testing analysis: 2026-05-25*
