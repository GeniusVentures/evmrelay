cmake_minimum_required(VERSION 3.15)

add_executable(test_discovery
    test_discovery.cpp
)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(test_discovery
    PRIVATE
    evmrelay
    Boost::json
)

# Disable ASAN: live-network integration test uses Boost.Asio coroutines which
# trigger false positives on macOS ARM64 (same reason as eth_watch).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_discovery PRIVATE -fno-sanitize=address)
endif()

# ── test_enr_survey ───────────────────────────────────────────────────────────
# Diagnostic live test: discovery-only (no dialing), no fork-id filter.
# Tallies eth_fork_id presence and hash frequency to diagnose live ENR failures.
add_executable(test_enr_survey
    test_enr_survey.cpp
)

add_executable(test_discv5_connect
    test_discv5_connect.cpp
)

target_link_libraries(test_enr_survey
    PRIVATE
    evmrelay
    Boost::boost
    Boost::json
)

target_link_libraries(test_discv5_connect
    PUBLIC
    evmrelay
    Boost::json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_enr_survey PRIVATE -fno-sanitize=address)
    target_compile_options(test_discv5_connect PRIVATE -fno-sanitize=address)
endif()

# Copy chains.json from examples/ alongside both discovery binaries at build time.
# Update examples/chains.json (no recompile needed) when fork hashes change.
add_custom_command(TARGET test_discovery POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../chains.json
        $<TARGET_FILE_DIR:test_discovery>/chains.json
    COMMENT "Copying chains.json to discovery binary directory"
)

add_custom_command(TARGET test_discv5_connect POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../chains.json
        $<TARGET_FILE_DIR:test_discv5_connect>/chains.json
    COMMENT "Copying chains.json to discv5 connect binary directory"
)

