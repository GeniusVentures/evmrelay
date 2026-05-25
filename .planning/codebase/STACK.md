# Technology Stack

**Analysis Date:** 2026-05-25

## Languages

**Primary:**
- C++17 - Core library (`src/`), headers (`include/`), examples (`examples/`), tests (`test/`)

**Secondary:**
- Go 1.24 - Chain peer crawler/generator tool (`rlp_enodes/` using `go-ethereum v1.17.1`)
- Shell (bash) - Test transaction scripts (`examples/send_test_transactions.sh`)
- Python - CI helper scripts, node filter script (`rlp_enodes/inspect_enrs.py`)

## Runtime

**Environment:**
- C++17 standard, no extensions (`cmake/toolchain/cxx17.cmake`, `CMakeLists.txt`)

**Package Manager:**
- CMake v3.16+ with `find_package` CONFIG mode
- No Conan, vcpkg, or other package manager
- Third-party dependencies pre-built externally, consumed via `THIRDPARTY_BUILD_DIR` and `ZKLLVM_BUILD_DIR` CMake variables
- Lockfile: Not applicable (CMake CONFIG-based resolution)

## Frameworks

**Core:**
- evmrelay v1.0.0 - Static library (`src/CMakeLists.txt`) providing RLP encoding/decoding, RLPx transport, discv4/discv5 discovery, ETH protocol handling
- Boost 1.85.0 - Coroutine-based async I/O (`Boost.Asio`, `Boost.Coroutine`, `Boost.Context`), JSON parsing (`Boost.JSON`), program options, logging, threading

**Testing:**
- Google Test (GTest) - Unit and integration tests (`test/` directory, 499+ tests)
- CMake CTest - Test runner via `ctest`
- `test_bin/` directory for test binaries

**Build/Dev:**
- Ninja - Build system generator (primary, used on all platforms)
- CMake 3.16+ - Build configuration
- Visual Studio 17 2022 - Windows generator (fallback)
- ClangFormat - Code formatting (`.clang-format`, BasedOnStyle: Microsoft with modifications)
- ClangTidy - Static analysis (`.clang-tidy` with ~50 checks enabled)
- ccache - Compiler cache (all CI platforms)

## Key Dependencies

**Critical:**
- Boost 1.85.0 - Async I/O, coroutines, JSON, logging, threading (components: date_time, filesystem, random, regex, system, thread, log, log_setup, program_options, json, context, coroutine)
- OpenSSL - ECDH key exchange, AES-256-CTR encryption, HMAC-SHA256, SHA3/Keccak hashing for RLPx/DevP2P transport security
- libsecp256k1 - secp256k1 elliptic curve operations for Ethereum key recovery and signing
- fmt - Type-safe string formatting (used by spdlog, `modernize-use-std-print` configured for `fmt::print`)
- spdlog v1.4.2 - Structured logging with `SPDLOG_FMT_EXTERNAL`

**Infrastructure:**
- Protobuf - Protocol Buffers serialization
- Snappy - Frame compression for discv4 discovery packets
- ZLIB - General compression support
- intx - Extended precision integer (`uint256`) for Ethereum values
- Microsoft GSL - Guidelines Support Library (header-only)
- crypto3 - zkLLVM cryptographic library (from GeniusVentures/zkLLVM)
- Boost.Outcome - Error propagation via `BOOST_OUTCOME_V2_NAMESPACE::result<T, E>`
- resolv - DNS resolver (system library for ENR-tree discovery)

## Configuration

**Environment:**
- Build controlled via CMake cache variables: `THIRDPARTY_BUILD_DIR`, `ZKLLVM_BUILD_DIR`
- Platform-specific build directories: `build/Linux/`, `build/OSX/`, `build/Windows/`, `build/Android/`, `build/iOS/`
- Build types: `Debug`, `Release`, `RelWithDebInfo`
- CI environment variables: `EVMRELAY_RUN_LIVE_ENR_TREE_TEST`, `EVMRELAY_LIVE_ENR_TREE_CHAIN`, `EVMRELAY_LIVE_ENR_TREE_SECONDS`
- RPC API keys via environment variable templates (e.g., `{key}` placeholder substitution)
- `.env` files (git-ignored) for test credentials (Foundry `cast` tooling)

**Build:**
- `cmake/toolchain/cxx17.cmake` - C++17 standard enforcement
- `cmake/CommonBuildParameters.cmake` - All third-party dependency paths
- `cmake/CompilationFlags.cmake` - Warning/error flags (-Wall, -Wextra, -Werror=sign-compare, etc.)
- `cmake/Sanitizers.cmake` - ASan, UBSan, TSan support
- `cmake/Valgrind.cmake` - Valgrind Memcheck integration
- `.clang-format` - C++17 standard, 120 column limit, 4-space indent, Allman braces, Microsoft-based
- `.clang-tidy` - Extensive checkset: boost-*, bugprone-*, cert-*, concurrency-*, cppcoreguidelines-*, modernize-*, performance-*, portability-*, readability-*

## Platform Requirements

**Development:**
- C++17-compatible compiler (Clang 10+, GCC 8+, MSVC 2019+)
- CMake 3.16+
- Ninja
- Pre-built thirdparty dependencies from `GeniusVentures/thirdparty` GitHub releases
- Pre-built zkLLVM from `GeniusVentures/zkLLVM` GitHub releases
- Optional: Foundry (`cast`) for test transaction generation
- Optional: Valgrind for memory leak detection

**Production:**
- Deployment targets: macOS (universal binary), Linux (x86_64, aarch64), Windows (x64), Android (arm64-v8a, armeabi-v7a), iOS (aarch64)
- Built as static library (`libevmrelay.a`) for embedding into SuperGenius mobile/desktop apps
- CMake install targets: `lib/cmake/evmrelay/`, `include/evmrelay/`

---

*Stack analysis: 2026-05-25*
