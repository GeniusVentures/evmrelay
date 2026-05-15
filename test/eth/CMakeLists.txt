# ETH Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# ETH messages tests
addtest(eth_messages_test
    eth_messages_test.cpp
)
target_link_libraries(eth_messages_test
    evmrelay
)

# ETH handshake tests — mirrors go-ethereum's eth/protocols/eth/handshake_test.go
addtest(eth_handshake_test
    eth_handshake_test.cpp
)
target_link_libraries(eth_handshake_test
    evmrelay
)

# ETH handshake guard tests
addtest(eth_handshake_guard_test
    eth_handshake_guard_test.cpp
)
target_link_libraries(eth_handshake_guard_test
    evmrelay
)

# ETH objects tests
addtest(eth_objects_test
    eth_objects_test.cpp
)
target_link_libraries(eth_objects_test
    evmrelay
)

# ETH watch integration tests
addtest(eth_watch_integration_test
    eth_watch_integration_test.cpp
)
target_link_libraries(eth_watch_integration_test
    evmrelay
)

# ETH transaction (type 0/1/2) encode/decode tests
addtest(eth_transactions_test
    eth_transactions_test.cpp
)
target_link_libraries(eth_transactions_test
    evmrelay
)

# EventFilter / EventWatcher tests
addtest(event_filter_test
    event_filter_test.cpp
)
target_link_libraries(event_filter_test
    evmrelay
)

# ABI decoder tests
addtest(abi_decoder_test
    abi_decoder_test.cpp
)
target_link_libraries(abi_decoder_test
    evmrelay
)

# EthWatchService tests
addtest(eth_watch_service_test
    eth_watch_service_test.cpp
)
target_link_libraries(eth_watch_service_test
    evmrelay
)

# Transport-neutral receipt source tests
addtest(eth_receipt_source_test
    eth_receipt_source_test.cpp
)
target_link_libraries(eth_receipt_source_test
    evmrelay
)

# Normalized bridge event tests
addtest(bridge_event_test
    bridge_event_test.cpp
)
target_link_libraries(bridge_event_test
    evmrelay
)

# Chain finality policy tests
addtest(finality_policy_test
    finality_policy_test.cpp
)
target_link_libraries(finality_policy_test
    evmrelay
)

# JSON-RPC encoding/parsing tests
addtest(json_rpc_test
    json_rpc_test.cpp
)
target_link_libraries(json_rpc_test
    evmrelay
)

# RPC receipt source tests
addtest(rpc_receipt_source_test
    rpc_receipt_source_test.cpp
)
target_link_libraries(rpc_receipt_source_test
    evmrelay
)

# EthWatchCli tests
addtest(eth_watch_cli_test
    eth_watch_cli_test.cpp
)
target_link_libraries(eth_watch_cli_test
    evmrelay
)

# ChainTracker tests
addtest(chain_tracker_test
    chain_tracker_test.cpp
)
target_link_libraries(chain_tracker_test
    evmrelay
)

# GNUS.AI contract address tests
addtest(gnus_contracts_test
    gnus_contracts_test.cpp
)
target_link_libraries(gnus_contracts_test
    evmrelay
)

# ETH peer session tests
addtest(eth_peer_session_test
    eth_peer_session_test.cpp
)
target_link_libraries(eth_peer_session_test
    evmrelay
)

# ETH watch runner tests
addtest(eth_watch_runner_test
    eth_watch_runner_test.cpp
)
target_link_libraries(eth_watch_runner_test
    evmrelay
)

# ETH watch mock peer tests
addtest(eth_watch_mock_peer_test
    eth_watch_mock_peer_test.cpp
)
target_link_libraries(eth_watch_mock_peer_test
    evmrelay
)
