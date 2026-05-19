# eth_watch example CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

add_executable(eth_watch
    eth_watch.cpp
)

add_executable(eth_watch_example_test
    eth_watch_example_test.cpp
)

# Include project headers
target_include_directories(eth_watch PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_include_directories(eth_watch_example_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(eth_watch
    PRIVATE
    evmrelay
)

target_link_libraries(eth_watch_example_test
    PRIVATE
    evmrelay
)

if(BUILD_TESTS)
    add_test(NAME eth_watch_example_test COMMAND eth_watch_example_test)
endif()

# boost::coroutines performs stack switching that confuses ASan on macOS ARM64:
# __asan_handle_no_return is called but ignored (stack region looks 1.5 GB),
# leaving redzones active on the coroutine stack and eventually crashing the
# error handler re-entrantly.  The unit tests (ctest) cover all logic with full
# ASan.  This binary's tests are network integration tests that need a clean run.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(eth_watch PRIVATE -fno-sanitize=address)
    target_compile_options(eth_watch_example_test PRIVATE -fno-sanitize=address)
endif()
