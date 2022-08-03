cmake_minimum_required(VERSION 3.10.1)

find_package(Catch2 REQUIRED)

add_executable(first_test test.cpp)

target_link_libraries(first_test
  PRIVATE
    Catch2::Catch2
)

include(CTest)
include(Catch)
catch_discover_tests(first_test)