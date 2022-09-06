cmake_minimum_required(VERSION 3.10.1)

find_package(Catch2 REQUIRED)

add_executable(first_test test.cpp)

target_include_directories(first_test
  PRIVATE
    libmfsync
)

target_link_libraries(first_test
  PRIVATE
    libmfsync
    Catch2::Catch2
)

add_custom_command(TARGET first_test PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/test_data $<TARGET_FILE_DIR:first_test>)

include(CTest)
include(Catch)
catch_discover_tests(first_test)
