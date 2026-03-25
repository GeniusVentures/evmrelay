# discv5 unit tests
cmake_minimum_required(VERSION 3.15)

addtest(discv5_enr_test
    ${CMAKE_CURRENT_LIST_DIR}/discv5_enr_test.cpp
)

target_link_libraries(discv5_enr_test
    evmrelay
)

target_include_directories(discv5_enr_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

addtest(discv5_bootnodes_test
    ${CMAKE_CURRENT_LIST_DIR}/discv5_bootnodes_test.cpp
)

target_link_libraries(discv5_bootnodes_test
    evmrelay
)

target_include_directories(discv5_bootnodes_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

addtest(discv5_crawler_test
    ${CMAKE_CURRENT_LIST_DIR}/discv5_crawler_test.cpp
)

target_link_libraries(discv5_crawler_test
    evmrelay
)

target_include_directories(discv5_crawler_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

addtest(discv5_client_test
    ${CMAKE_CURRENT_LIST_DIR}/discv5_client_test.cpp
)

target_link_libraries(discv5_client_test
    evmrelay
)

target_include_directories(discv5_client_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)


