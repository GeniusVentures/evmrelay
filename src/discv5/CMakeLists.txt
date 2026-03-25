# discv5 peer discovery library
add_library(discv5 OBJECT
        ${CMAKE_CURRENT_LIST_DIR}/discv5_error.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv5_enr.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv5_bootnodes.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv5_crawler.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv5_client.cpp
)

# Link public dependencies
target_link_libraries(discv5 PUBLIC
        libsecp256k1::secp256k1
        OpenSSL::Crypto
        spdlog::spdlog
        fmt::fmt
)

target_include_directories(discv5 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${crypto3_INCLUDE_DIR}>
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../include/discv5/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/discv5
        FILES_MATCHING PATTERN "*.hpp"
)


