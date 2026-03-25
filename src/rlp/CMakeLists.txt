# Define the static library target
add_library(rlp OBJECT
        ${CMAKE_CURRENT_LIST_DIR}/rlp_encoder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/rlp_decoder.cpp
        ${CMAKE_CURRENT_LIST_DIR}/rlp_streaming.cpp
        ${CMAKE_CURRENT_LIST_DIR}/common.cpp
        ${CMAKE_CURRENT_LIST_DIR}/endian.cpp
)

# Link public dependencies
target_link_libraries(rlp PUBLIC
        libsecp256k1::secp256k1
        spdlog::spdlog
        fmt::fmt
)

target_include_directories(rlp PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${crypto3_INCLUDE_DIR}>
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../include/rlp/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rlp
        FILES_MATCHING PATTERN "*.hpp"
)

