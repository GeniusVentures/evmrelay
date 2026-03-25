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

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../include/rlp/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rlp
        FILES_MATCHING PATTERN "*.hpp"
)

