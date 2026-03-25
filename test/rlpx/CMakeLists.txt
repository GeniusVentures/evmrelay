# RLPx Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Crypto tests
addtest(rlpx_crypto_tests
    ${CMAKE_CURRENT_LIST_DIR}/crypto_test.cpp
)

target_link_libraries(rlpx_crypto_tests
    evmrelay
)

target_include_directories(rlpx_crypto_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Frame cipher tests
addtest(rlpx_frame_cipher_tests
    ${CMAKE_CURRENT_LIST_DIR}/frame_cipher_test.cpp
)

target_link_libraries(rlpx_frame_cipher_tests
    evmrelay
)

target_include_directories(rlpx_frame_cipher_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Protocol messages tests
addtest(rlpx_protocol_messages_tests
    ${CMAKE_CURRENT_LIST_DIR}/protocol_messages_test.cpp
)

target_link_libraries(rlpx_protocol_messages_tests
    evmrelay
)

target_include_directories(rlpx_protocol_messages_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# RLPx Session integration tests
addtest(rlpx_session_tests
    ${CMAKE_CURRENT_LIST_DIR}/rlpx_session_test.cpp
)

target_link_libraries(rlpx_session_tests
    evmrelay
)

target_include_directories(rlpx_session_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# RLPx state tests
addtest(rlpx_state_tests
    ${CMAKE_CURRENT_LIST_DIR}/rlpx_state_test.cpp
)

target_link_libraries(rlpx_state_tests
    evmrelay
)

target_include_directories(rlpx_state_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# RLPx message routing tests
addtest(rlpx_message_routing_tests
    ${CMAKE_CURRENT_LIST_DIR}/message_routing_test.cpp
)

target_link_libraries(rlpx_message_routing_tests
    evmrelay
)

target_include_directories(rlpx_message_routing_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# RLPx socket lifecycle tests
addtest(rlpx_socket_lifecycle_tests
    ${CMAKE_CURRENT_LIST_DIR}/socket_lifecycle_test.cpp
)

target_link_libraries(rlpx_socket_lifecycle_tests
    evmrelay
)

target_include_directories(rlpx_socket_lifecycle_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Handshake key derivation vectors test (go-ethereum compatible)
addtest(rlpx_handshake_vectors_tests
    ${CMAKE_CURRENT_LIST_DIR}/handshake_vectors_test.cpp
)

target_link_libraries(rlpx_handshake_vectors_tests
    evmrelay
)

target_include_directories(rlpx_handshake_vectors_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Snappy compression tests (go-ethereum wire vectors + live peer capture)
addtest(rlpx_snappy_tests
    ${CMAKE_CURRENT_LIST_DIR}/snappy_test.cpp
)

target_link_libraries(rlpx_snappy_tests
    evmrelay
)

target_include_directories(rlpx_snappy_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

