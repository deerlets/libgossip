cmake_minimum_required(VERSION 2.8)

set(CMAKE_CXX_STANDARD 11)
set(PROJECT_NAME libgossip-test)

project(${PROJECT_NAME} C CXX)
add_definitions(-DWITH_TESTS)

# gossip
add_executable(runGossipTests gossip_test.cpp)
target_link_libraries(runGossipTests gtest gtest_main gossip pthread)
add_test(runGossipTests ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/runGossipTests)
