# discv5_crawl — live peer discovery example binary
cmake_minimum_required(VERSION 3.15)

add_executable(discv5_crawl
    ${CMAKE_CURRENT_LIST_DIR}/discv5_crawl.cpp
)

target_link_libraries(discv5_crawl PRIVATE
    evmrelay
    Boost::boost
)

target_include_directories(discv5_crawl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(discv5_crawl PRIVATE cxx_std_17)

