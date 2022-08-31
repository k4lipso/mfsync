#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

#include "mfsync/file_handler.h"

unsigned int Factorial( unsigned int number ) {
    return number <= 1 ? number : Factorial(number-1)*number;
}

TEST_CASE( "mfsync example test", "[example_test]" ) {
  auto handler = mfsync::file_handler();
  const auto stored = handler.get_stored_files();
  REQUIRE(stored.empty());
}

TEST_CASE( "Factorials are computed", "[factorial]" ) {
    REQUIRE( Factorial(1) == 1 );
    REQUIRE( Factorial(2) == 2 );
    REQUIRE( Factorial(3) == 6 );
    REQUIRE( Factorial(10) == 3628800 );
}
