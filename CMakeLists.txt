cmake_minimum_required (VERSION 3.2.2)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)

project("mfsync" CXX)

option(BUILD_TESTS "test will be build" OFF)
option(BUILD_DOC "build documentation" OFF)
option(USE_SUBMODULES "built using submodules. this is only needed when building without nix" OFF)
option(BUILD_STATIC "static link libmfsync" OFF)

add_compile_options(
  -Wall
  -Wextra
  $<$<CONFIG:DEBUG>:-U_FORTIFY_SOURCE>
  $<$<CONFIG:DEBUG>:-g>
  $<$<CONFIG:DEBUG>:-O0>
  $<$<CONFIG:RELEASE>:-O3>
  )

message("Build Type: ${CMAKE_BUILD_TYPE}")
message("Compile Opts: ${COMPILE_OPTIONS}")


find_package(Threads REQUIRED)
find_package(OpenSSL MODULE REQUIRED COMPONENTS SSL Crypto EXACT)

if(NOT TARGET spdlog)
  find_package(spdlog REQUIRED)
endif()

SET(Boost_USE_MULTITHREAD ON)
FIND_PACKAGE(Boost 1.71.0 REQUIRED COMPONENTS program_options system thread regex filesystem)

message("Boost information")
message("Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message("Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")
message("Boost Libraries: ${Boost_LIBRARIES}")

set(LIBMFSYNC_FILES
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
  src/progress_handler.cpp
  )

if(BUILD_STATIC)
add_library(libmfsync STATIC
  ${LIBMFSYNC_FILES}
)
else()
add_library(libmfsync SHARED
  ${LIBMFSYNC_FILES}
)
endif()

target_include_directories(libmfsync
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    ${OPENSSL_INCLUDE_DIR}
)


if(USE_SUBMODULES)
  include(FetchContent)
  Set(FETCHCONTENT_QUIET FALSE)
  FetchContent_Declare(
    libindicators
    GIT_REPOSITORY https://github.com/p-ranav/indicators
    GIT_TAG        a5bc05f32a9c719535054b7fa5306ce5c8d055d8
    GIT_SHALLOW TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    GIT_PROGRESS TRUE
  )

  FetchContent_MakeAvailable(libindicators)
  target_include_directories(libmfsync
    PUBLIC
      "${libindicators_SOURCE_DIR}/include"
  )

  FetchContent_Declare(
    libnlohmann-json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG        4f8fba14066156b73f1189a2b8bd568bde5284c5
    GIT_SHALLOW TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    GIT_PROGRESS TRUE
  )

  FetchContent_MakeAvailable(libnlohmann-json)
  target_include_directories(libmfsync
    PUBLIC
      "${libnlohmann-json_SOURCE_DIR}/include"
  )
endif()

message("OpenSSL::Crypto: ${OPENSSL_LIBRARIES}")

target_link_libraries(libmfsync
  PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
)

add_executable(mfsync
  src/main.cpp
)

add_dependencies(mfsync libmfsync)

target_include_directories(mfsync
  PRIVATE
    libmfsync
)

target_link_libraries(mfsync
  PRIVATE
    libmfsync
    Boost::program_options
    spdlog::spdlog
)

target_compile_options(mfsync
  PUBLIC
  -DBOOST_LOG_DYN_LINK
  )

target_compile_options(libmfsync
  PUBLIC
  -DBOOST_LOG_DYN_LINK
  )

if(BUILD_TESTS)
  add_subdirectory(test/unit)
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
