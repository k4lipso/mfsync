cmake_minimum_required (VERSION 3.2.2)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Release)

project("mfsync" CXX)

option(BUILD_TESTS "test will be build" OFF)
option(BUILD_DOC "build documentation" OFF)
option(USE_SUBMODULES "built using submodules. this is only needed when building without nix" OFF)
option(DEBUG "set cmake build type to debug" OFF)

if(DEBUG)
  set(CMAKE_BUILD_TYPE Debug)
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -U_FORTIFY_SOURCE -Wall -Wextra -g -O0")
endif()



find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto EXACT)

if(NOT TARGET spdlog)
  find_package(spdlog REQUIRED)
endif()

SET(Boost_USE_MULTITHREAD ON)
FIND_PACKAGE(Boost 1.71.0 REQUIRED COMPONENTS program_options system thread regex filesystem)

message("Boost information")
message("Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message("Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")
message("Boost Libraries: ${Boost_LIBRARIES}")


add_executable(mfsync
  src/main.cpp
  src/file_information.cpp
  src/file_handler.cpp
  src/ofstream_wrapper.cpp
  src/file_sender.cpp
  src/file_fetcher.cpp
  src/file_receive_handler.cpp
  src/protocol.cpp
  src/deque.cpp
  src/server_session.cpp
  src/client_session.cpp
  src/server.cpp
  )

target_include_directories(mfsync
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${OPENSSL_INCLUDE_DIR}
)

message("OpenSSL::Crypto: ${OPENSSL_LIBRARIES}")

target_link_libraries(mfsync
  PRIVATE
    Boost::program_options
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

target_compile_options(mfsync
  PUBLIC
  -DBOOST_LOG_DYN_LINK
  )

if(BUILD_TESTS)
  add_subdirectory(test)
endif()

if(BUILD_DOC)
  # look for Doxygen package
  find_package(Doxygen)
  if(DOXYGEN_FOUND)
    message("Found Doxygen")
    # exclude sqlite code
    set(DOXYGEN_EXCLUDE_PATTERNS
      */sqlite3/*
      )
    # doxygen settings can be set here, prefixed with "DOXYGEN_"
    set(DOXYGEN_SOURCE_BROWSER YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/api-docs")
    # this target will only be built if specifically asked to.
    # run "make api-docs" to create the doxygen documentation
    doxygen_add_docs(
      api-docs
      ${PROJECT_SOURCE_DIR}
      COMMENT "Generate API-documents for NoteSearch."
      )
  endif(DOXYGEN_FOUND)
endif()

message("End")
