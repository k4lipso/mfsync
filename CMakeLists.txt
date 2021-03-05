cmake_minimum_required (VERSION 3.2.2)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Debug)

project("mdump" CXX)

option(BUILD_TESTS "test will be build" OFF)
option(BUILD_DOC "build documentation" OFF)
option(USE_SUBMODULES "built using submodules. this is only needed when building without nix" OFF)

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

if(NOT TARGET spdlog)
    find_package(spdlog REQUIRED)
endif()

SET(Boost_USE_MULTITHREAD ON)
FIND_PACKAGE(Boost 1.71.0 REQUIRED COMPONENTS program_options system thread regex filesystem)

message("Boost information")
message("Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message("Boost_LIBRARY_DIRS: ${Boost_LIBRARY_DIRS}")
message("Boost Libraries: ${Boost_LIBRARIES}")


#add_library(libmdump
#    )
#
#set_target_properties(libmdump PROPERTIES LINKER_LANGUAGE CXX)
#
#target_include_directories(libmdump
#    PUBLIC
#    if(USE_SUBMODULES)
#        "external/sqlite_modern_cpp/hdr"
#    endif()
#        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#        $<INSTALL_INTERFACE:include>
#        ${Boost_INCLUDE_DIRS}
#        "${CMAKE_CURRENT_BINARY_DIR}/include/mdump"
#    )
#
#target_link_libraries(libmdump
#    PUBLIC
#        ${CMAKE_THREAD_LIBS_INIT}
#        Boost::system
#        Boost::thread
#        Boost::regex
#        Boost::filesystem
#        ${SQLite3_LIBRARIES}
#    )
#
#  target_compile_options(libmdump
#    PUBLIC
#    -DBOOST_LOG_DYN_LINK
#    )

add_executable(mdump src/main.cpp)

target_include_directories(mdump
    PUBLIC
)

target_link_libraries(mdump
    PRIVATE
        Boost::program_options
	spdlog::spdlog
	#libmdump
)

target_compile_options(mdump
  PUBLIC
  -DBOOST_LOG_DYN_LINK
  )

if(BUILD_TESTS)
    add_subdirectory(test)
endif()


set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -U_FORTIFY_SOURCE -O0 -Wall -Wextra -g")

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
