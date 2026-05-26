---
plan_id: 01-CHAINLIST
phase: 01
wave: 1
depends_on: []
autonomous: true
files_modified:
  - include/eth/chainlist_provider.hpp
  - src/eth/chainlist_provider.cpp
  - test/eth/chainlist_provider_test.cpp
  - test/eth/CMakeLists.txt
requirements:
  - CHNL-01
  - CHNL-02
  - CHNL-03
  - CHNL-04
  - CHNL-05
  - CODE-01
project_root: /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay
---

# Plan: ChainList Provider Implementation

Parse `chainid.network/chains.json` using schema-driven JSON, filter to configured chains,
deduplicate/validate RPC URLs, and produce `std::vector<RpcEndpointConfig>`.

## must_haves

- Schema-driven parser using `JsonSchemaObject` produces valid `RpcEndpointConfig` candidates
- Only chains present in `examples/chains_config.json` are included in output
- API-key placeholder URLs are filtered out
- Deprecated chains are excluded by default
- `intx.hpp` header documents upstream provenance

---

<task>
<objective>Declare chainlist_provider.hpp header</objective>
<type>execute</type>
<read_first>
- include/eth/rpc_manager_config.hpp
- include/base/json_utility.hpp
</read_first>
<action>
Create `include/eth/chainlist_provider.hpp` under `#ifndef EVMRELAY_INCLUDE_ETH_CHAINLIST_PROVIDER_HPP`.

Declare in namespace `eth::rpc`:

1. Free function `load_chainlist_from_json_text(std::string_view json_text) -> rlp::base::json::JsonResult<std::vector<RpcEndpointConfig>>`
   - Accepts raw `chains.json` text
   - Parses the top-level JSON array using `JsonSchemaArray` (element type `kObject`, schema defined inline)
   - For each chain object, parses: `name` (string), `chainId` (u64), `rpc` (array of strings), `status` (string, optional, default "active"), `shortName` (string)
   - Skips entries where `status == "deprecated"`
   - For each `rpc[]` URL: filters out `wss://`, filters out API-key placeholders (`${INFURA_API_KEY}`, `${ALCHEMY_API_KEY}`, `${ANKR_API_KEY}`, `${POKT_API_KEY}`, `${BLASTAPI_API_KEY}`), filters out malformed URLs
   - Constructs one `RpcEndpointConfig` per surviving URL with:
     - `chain_name = name`
     - `chain_id = chainId`
     - `url_template = url`
     - `priority = 0`, `weight = 0`, `rate_limit_per_second = 0`
     - `is_public = true`, `is_paid = false`, `verified = false`
   - Deduplicates by `chainId + URL` before returning

2. Free function `filter_to_configured_chains(std::vector<RpcEndpointConfig>& endpoints, const std::vector<discv4::ChainPeerConfig>& configured_chains) -> std::vector<RpcEndpointConfig>`
   - Keeps only endpoints whose `chain_id` matches a configured chain
   - Returns deduplicated filtered vector
</action>
<acceptance_criteria>
- Header guard uses `#ifndef EVMRELAY_INCLUDE_ETH_CHAINLIST_PROVIDER_HPP`
- `[[nodiscard]]` on all result-returning functions
- Includes referenced types (`include/eth/rpc_manager_config.hpp`, `include/base/json_utility.hpp`)
- No classes — free functions only following `rpc_manager_config.hpp` pattern
</acceptance_criteria>
</task>

---

<task>
<objective>Implement chainlist_provider.cpp</objective>
<type>execute</type>
<depends_on>plan:01-CHAINLIST:Declare chainlist_provider.hpp header</depends_on>
<read_first>
- include/eth/chainlist_provider.hpp
- src/eth/rpc_manager_config.cpp
- include/base/json_utility.hpp
</read_first>
<action>
Create `src/eth/chainlist_provider.cpp`.

Implement `load_chainlist_from_json_text()`:

1. Define `chain_entry_schema()` returning a `JsonSchemaObject` with fields:
   - `name` (kString, required)
   - `chainId` (kU64, required)
   - `rpc` (kArray, required, element kString)
   - `status` (kString, optional, default `"active"`)
   - `shortName` (kString, optional, default `""`)

2. Define `rpc_array_schema()` returning `JsonSchemaArray` with element `kString`

3. Parse the top-level `chains.json` array using `JsonSchemaArray` (element `kObject`, schema `chain_entry_schema()`).
   Since `parse_schema_object` works on a single object, handle the array manually:
   - Parse the JSON text with `boost::json::parse(json_text)`
   - Expect root is an array; iterate over elements
   - For each element (an object), call `parse_schema_object(obj, chain_entry_schema())`
   - `BOOST_OUTCOME_TRY` each parse; skip entries that fail parsing (log warning, continue)

4. For each parsed entry, apply filters:
   - Skip if `status` is `"deprecated"`
   - For each `rpc[]` URL: skip if starts with `wss://`
   - Skip if contains `${INFURA_API_KEY}` or `${ALCHEMY_API_KEY}` or `${ANKR_API_KEY}` or `${POKT_API_KEY}` or `${BLASTAPI_API_KEY}`
   - Skip if URL does not start with `http://` or `https://`

5. Build `RpcEndpointConfig` for each surviving URL with defaults

6. Deduplicate: use `std::unordered_set<std::string>` keyed on `std::to_string(chain_id) + "|" + url`; only add if not in set

Implement `filter_to_configured_chains()`:
   - Build `std::unordered_set<uint64_t>` of configured chain IDs
   - `std::copy_if` to filter endpoints; only keep those in the set
</action>
<acceptance_criteria>
- `src/eth/chainlist_provider.cpp` has `#include <eth/chainlist_provider.hpp>`
- Uses anonymous namespace for internal helpers and schema definitions
- `BOOST_OUTCOME_TRY` for error propagation
- API-key placeholders `${INFURA_API_KEY}`, `${ALCHEMY_API_KEY}`, `${ANKR_API_KEY}`, `${POKT_API_KEY}`, `${BLASTAPI_API_KEY}` are filtered
- `wss://` URLs are excluded
- Deprecated chains (`status == "deprecated"`) are excluded
- Deduplication by chainId+URL prevents duplicate endpoints
</acceptance_criteria>
</task>

---

<task>
<objective>Register chainlist_provider test in CMakeLists.txt</objective>
<type>execute</type>
<depends_on>plan:01-CHAINLIST:Implement chainlist_provider.cpp</depends_on>
<read_first>
- test/eth/CMakeLists.txt
</read_first>
<action>
Add to `test/eth/CMakeLists.txt` near the bridge_event_test entry:

```cmake
addtest(chainlist_provider_test
    chainlist_provider_test.cpp
)
target_link_libraries(chainlist_provider_test
    evmrelay
)
```
</action>
<acceptance_criteria>
- `test/eth/CMakeLists.txt` contains `addtest(chainlist_provider_test`
- `target_link_libraries(chainlist_provider_test evmrelay)` follows
</acceptance_criteria>
</task>

---

<task>
<objective>Write chainlist_provider_test.cpp with schema-driven fixture tests</objective>
<type>execute</type>
<depends_on>plan:01-CHAINLIST:Register chainlist_provider test in CMakeLists.txt</depends_on>
<read_first>
- include/eth/chainlist_provider.hpp
- include/base/json_utility.hpp
- test/eth/bridge_event_test.cpp
</read_first>
<action>
Create `test/eth/chainlist_provider_test.cpp`.

Use `TEST(ChainlistProviderTest, ...)` naming.

Test cases:
1. **ParsesValidAggregatedJson** — Valid `chains.json` with 2 chains, each with 2 RPC URLs. Assert 4 endpoints returned, verify chain_name and chain_id fields.
2. **FiltersDeprecatedChains** — Input has one chain with `status: "deprecated"`. Assert zero endpoints.
3. **DefaultsMissingStatusToActive** — Input has no `status` field. Assert chain is included.
4. **FiltersApiKeyPlaceholders** — Input has URLs containing `${INFURA_API_KEY}` and `${ALCHEMY_API_KEY}`. Assert those URLs excluded, plain URLs kept.
5. **FiltersWebsocketUrls** — Input has `wss://...` URLs. Assert excluded.
6. **FiltersMalformedUrls** — Input has empty URL string, missing scheme. Assert excluded.
7. **DeduplicatesRepeatedEndpoints** — Same chainId+URL appears twice. Assert only one endpoint.
8. **ExcludesChainsNotInConfig** — Uses `filter_to_configured_chains()` with a configured chain set. Unconfigured chain excluded.
9. **RejectsInvalidJson** — Malformed JSON. Assert error returned.
10. **HandlesEmptyArray** — Empty `chains.json` array. Assert empty result, no error.

Fixture: embed JSON strings inline as `const char*` in each test. No separate fixture files needed.

Follow `bridge_event_test.cpp` patterns: `#include <gtest/gtest.h>`, anonymous namespace, inline JSON literals.
</action>
<acceptance_criteria>
- 10 test cases covering parser, filters, dedup, error handling
- No network I/O — all tests use inline JSON strings
- Tests pass with `./test_bin/chainlist_provider_test`
- No `std::this_thread::sleep_for` — use Arrange-Act-Assert pattern
- Includes `#include <eth/chainlist_provider.hpp>`
</acceptance_criteria>
</task>

---

<task>
<objective>Build and run chainlist_provider tests</objective>
<type>verify</type>
<depends_on>plan:01-CHAINLIST:Write chainlist_provider_test.cpp with schema-driven fixture tests</depends_on>
<read_first>
- test/eth/CMakeLists.txt
</read_first>
<action>
```bash
cd build/OSX/Debug && cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && ninja chainlist_provider_test && ./test_bin/chainlist_provider_test
```

All tests must pass.
</action>
<acceptance_criteria>
- `ninja chainlist_provider_test` exits 0
- `./test_bin/chainlist_provider_test` exits 0 with 10 passing tests
</acceptance_criteria>
</task>
