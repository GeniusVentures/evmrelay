# RLPx CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Define the library target
add_library(rlpx OBJECT
    ${CMAKE_CURRENT_LIST_DIR}/rlpx_error.cpp
    ${CMAKE_CURRENT_LIST_DIR}/auth/ecies_cipher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/auth/auth_handshake.cpp
    ${CMAKE_CURRENT_LIST_DIR}/crypto/kdf.cpp
    ${CMAKE_CURRENT_LIST_DIR}/crypto/ecdh.cpp
    ${CMAKE_CURRENT_LIST_DIR}/crypto/hmac.cpp
    ${CMAKE_CURRENT_LIST_DIR}/crypto/aes.cpp
    ${CMAKE_CURRENT_LIST_DIR}/framing/frame_cipher.cpp
    ${CMAKE_CURRENT_LIST_DIR}/framing/message_stream.cpp
    ${CMAKE_CURRENT_LIST_DIR}/protocol/messages.cpp
    ${CMAKE_CURRENT_LIST_DIR}/socket/socket_transport.cpp
    ${CMAKE_CURRENT_LIST_DIR}/rlpx_session.cpp
)

# Link public dependencies
target_link_libraries(rlpx PUBLIC
        Boost::boost
        libsecp256k1::secp256k1
        OpenSSL::SSL
        OpenSSL::Crypto
        Snappy::snappy
        spdlog::spdlog
        fmt::fmt
)

# Specify public include directory
target_include_directories(rlpx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${crypto3_INCLUDE_DIR}>
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../include/rlpx/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rlpx
    FILES_MATCHING PATTERN "*.hpp"
)
