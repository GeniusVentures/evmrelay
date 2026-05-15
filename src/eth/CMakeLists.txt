# eth peer discovery library
add_library(eth OBJECT
        ${CMAKE_CURRENT_LIST_DIR}/messages.cpp
        ${CMAKE_CURRENT_LIST_DIR}/objects.cpp
        ${CMAKE_CURRENT_LIST_DIR}/event_filter.cpp
        ${CMAKE_CURRENT_LIST_DIR}/abi_decoder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/chain_tracker.cpp
        ${CMAKE_CURRENT_LIST_DIR}/eth_watch_service.cpp
        ${CMAKE_CURRENT_LIST_DIR}/eth_peer_session.cpp
        ${CMAKE_CURRENT_LIST_DIR}/eth_handshake_guard.cpp
        ${CMAKE_CURRENT_LIST_DIR}/eth_session_channel.cpp
        ${CMAKE_CURRENT_LIST_DIR}/eth_receipt_source.cpp
        ${CMAKE_CURRENT_LIST_DIR}/bridge_event.cpp
        ${CMAKE_CURRENT_LIST_DIR}/finality_policy.cpp
        ${CMAKE_CURRENT_LIST_DIR}/json_rpc.cpp
        ${CMAKE_CURRENT_LIST_DIR}/eth_watch_runner.cpp
)

target_include_directories(eth PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${crypto3_INCLUDE_DIR}>
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../include/eth/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/eth
        FILES_MATCHING PATTERN "*.hpp"
)
