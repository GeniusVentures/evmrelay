# Discovery v4 Protocol Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Discovery tests
addtest(discv4_discovery_tests
    ${CMAKE_CURRENT_LIST_DIR}/discovery_test.cpp
)

target_link_libraries(discv4_discovery_tests
    evmrelay
)

target_include_directories(discv4_discovery_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Protocol tests
addtest(discv4_protocol_test
    ${CMAKE_CURRENT_LIST_DIR}/discv4_protocol_test.cpp
)

target_link_libraries(discv4_protocol_test
    evmrelay
)

target_include_directories(discv4_protocol_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# DialHistory tests (header-only, no extra libs needed)
addtest(discv4_dial_history_test
    ${CMAKE_CURRENT_LIST_DIR}/dial_history_test.cpp
)

target_include_directories(discv4_dial_history_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# discv4_client lifetime tests
addtest(discv4_client_test
    ${CMAKE_CURRENT_LIST_DIR}/discv4_client_test.cpp
)

target_link_libraries(discv4_client_test
     evmrelay
)

target_include_directories(discv4_client_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# DialScheduler placeholder test
addtest(discv4_dial_scheduler_test
    ${CMAKE_CURRENT_LIST_DIR}/dial_scheduler_test.cpp
)

target_link_libraries(discv4_dial_scheduler_test
    evmrelay
)

target_include_directories(discv4_dial_scheduler_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# ENRRequest wire encode tests
addtest(discv4_enr_request_test
    ${CMAKE_CURRENT_LIST_DIR}/enr_request_test.cpp
)

target_link_libraries(discv4_enr_request_test
    evmrelay
)

target_include_directories(discv4_enr_request_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# ENRResponse wire decode tests
addtest(discv4_enr_response_test
    ${CMAKE_CURRENT_LIST_DIR}/enr_response_test.cpp
)

target_link_libraries(discv4_enr_response_test
    evmrelay
)

target_include_directories(discv4_enr_response_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# ENR client (request_enr) integration tests
addtest(discv4_enr_client_test
    ${CMAKE_CURRENT_LIST_DIR}/enr_client_test.cpp
)

target_link_libraries(discv4_enr_client_test
    evmrelay
)

target_include_directories(discv4_enr_client_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# ENR enrichment flow tests (DiscoveredPeer.eth_fork_id population)
addtest(discv4_enr_enrichment_test
    ${CMAKE_CURRENT_LIST_DIR}/enr_enrichment_test.cpp
)

target_link_libraries(discv4_enr_enrichment_test
    evmrelay
)

target_include_directories(discv4_enr_enrichment_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)


# ForkId filter + DialScheduler integration tests
addtest(discv4_dial_filter_test
    ${CMAKE_CURRENT_LIST_DIR}/dial_filter_test.cpp
)

target_link_libraries(discv4_dial_filter_test
    evmrelay
)

target_include_directories(discv4_dial_filter_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_tests_properties(discv4_client_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=halt_on_error=0")
set_tests_properties(discv4_enr_client_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=halt_on_error=0")
set_tests_properties(discv4_enr_enrichment_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=halt_on_error=0")
set_tests_properties(discv4_dial_filter_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=halt_on_error=0")
