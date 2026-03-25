add_library(rlp_logger OBJECT
    ../../include/base/rlp-logger.hpp
    rlp-logger.cpp
)

target_include_directories(rlp_logger
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(rlp_logger
    PUBLIC
    spdlog::spdlog
    fmt::fmt
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../../include/base/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/base
        FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include/discovery/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/discovery
        FILES_MATCHING PATTERN "*.hpp"
)
