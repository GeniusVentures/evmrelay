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


