include_directories(${crypto3_INCLUDE_DIR})

# Set config of secp256k1 project
find_package(libsecp256k1 CONFIG REQUIRED)
# Set config of speedlog project
find_package(fmt CONFIG REQUIRED)
# Set config of speedlog project
find_package(spdlog CONFIG REQUIRED)

# Add base subdirectory
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/base ${CMAKE_CURRENT_BINARY_DIR}/base)
# Add eth subdirectory
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/eth ${CMAKE_CURRENT_BINARY_DIR}/eth)
# Add rlp subdirectory
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/rlp ${CMAKE_CURRENT_BINARY_DIR}/rlp)
# Add RLPx subdirectory
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/rlpx ${CMAKE_CURRENT_BINARY_DIR}/rlpx)
# Add discv4 subdirectory
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/discv4 ${CMAKE_CURRENT_BINARY_DIR}/discv4)
# Add discv5 subdirectory
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/discv5 ${CMAKE_CURRENT_BINARY_DIR}/discv5)


# Define the static library target
add_library(evmrelay STATIC
        $<TARGET_OBJECTS:eth>
        $<TARGET_OBJECTS:rlp_logger>
        $<TARGET_OBJECTS:rlp>
        $<TARGET_OBJECTS:rlpx>
        $<TARGET_OBJECTS:discv4>
        $<TARGET_OBJECTS:discv5>
)

# Specify public include directory for BUILD interface
target_include_directories(evmrelay PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../include>
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${crypto3_INCLUDE_DIR}>
)

# Link public dependencies
target_link_libraries(evmrelay PUBLIC
    Boost::boost
    Boost::context
    Boost::coroutine
    libsecp256k1::secp256k1
    OpenSSL::SSL
    OpenSSL::Crypto
    Snappy::snappy
    spdlog::spdlog
    fmt::fmt
)

# Install the core library and attach it to the export set consumed by
# the top-level install(EXPORT rlp ...) call.
install(TARGETS evmrelay
    EXPORT evmrelay
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

