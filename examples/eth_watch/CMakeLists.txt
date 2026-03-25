# eth_watch example CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

add_executable(eth_watch
    eth_watch.cpp
)

# Include project headers
include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(eth_watch
    PUBLIC
    evmrelay
    Boost::json
)

# boost::coroutines performs stack switching that confuses ASan on macOS ARM64:
# __asan_handle_no_return is called but ignored (stack region looks 1.5 GB),
# leaving redzones active on the coroutine stack and eventually crashing the
# error handler re-entrantly.  The unit tests (ctest) cover all logic with full
# ASan.  This binary's tests are network integration tests that need a clean run.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(eth_watch PRIVATE -fno-sanitize=address)
endif()

