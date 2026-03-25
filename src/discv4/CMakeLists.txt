# Define the static library target
add_library(discv4 OBJECT
        ${CMAKE_CURRENT_LIST_DIR}/packet_factory.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv4_packet.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv4_ping.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv4_pong.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv4_error.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv4_client.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv4_enr_request.cpp
        ${CMAKE_CURRENT_LIST_DIR}/discv4_enr_response.cpp
)

# Link public dependencies
target_link_libraries(discv4 PUBLIC
        libsecp256k1::secp256k1
        spdlog::spdlog
        fmt::fmt
)

target_include_directories(discv4 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${crypto3_INCLUDE_DIR}>
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../include/discv4/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/discv4
        FILES_MATCHING PATTERN "*.hpp"
)


