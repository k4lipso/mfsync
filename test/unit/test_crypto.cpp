#define CATCH_CONFIG_MAIN

#include <cryptopp/chachapoly.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/osrng.h>
#include <cryptopp/xed25519.h>

#include <catch2/catch.hpp>

#include "mfsync/crypto.h"
#include "spdlog/spdlog.h"

TEST_CASE("crypto base test", "[crypto]") {
  spdlog::info("Generating private key:");

  using namespace mfsync::crypto;
  using namespace CryptoPP;

  crypto_handler A, B;
  A.init("testA.key");
  B.init("testB.key");

  A.trust_key(B.get_public_key());
  B.trust_key(A.get_public_key());

  std::string test_msg{"This is a test message"};
  std::string aad_msg{"This is unencrypted info"};

  const auto encr = A.encrypt(B.get_public_key(), test_msg, aad_msg);
  REQUIRE(encr.has_value());

  const auto decr = B.decrypt(A.get_public_key(), encr.value());
  REQUIRE(decr.has_value());
}
