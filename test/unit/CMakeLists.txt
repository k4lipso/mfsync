cmake_minimum_required(VERSION 3.10.1)

find_package(Catch2 REQUIRED)

add_executable(base_test test.cpp)

target_include_directories(base_test
  PRIVATE
    libmfsync
)

target_link_libraries(base_test
  PRIVATE
    libmfsync
    Catch2::Catch2
)

add_custom_command(TARGET base_test PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/test_data $<TARGET_FILE_DIR:base_test>)

add_executable(crypto_test test_crypto.cpp)

target_include_directories(crypto_test
  PRIVATE
    libmfsync
)

target_link_libraries(crypto_test
  PRIVATE
    libmfsync
    Catch2::Catch2
)


include(CTest)
include(Catch)
catch_discover_tests(base_test)
catch_discover_tests(crypto_test)
