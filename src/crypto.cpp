#include "mfsync/crypto.h"
#include "spdlog/spdlog.h"

#include <cryptopp/chachapoly.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/chachapoly.h>
#include <cryptopp/filters.h>
#include <cryptopp/files.h>
#include <cryptopp/hex.h>
#include <cryptopp/xed25519.h>
#include <cryptopp/osrng.h>

namespace mfsync::crypto
{

void test()
{
  spdlog::info("Generating private key:");

  using namespace CryptoPP;

  crypto_handler A, B;
  A.init("testA.key");
  B.init("testB.key");

  A.trust_key(B.get_public_key());
  B.trust_key(A.get_public_key());

  std::string test_msg{"This is a test message"};
  std::string aad_msg{"This is unencrypted info"};

  std::cout << "orig: " << test_msg << '\n';
  std::cout << "aad : " << aad_msg << '\n';

  const auto encr  = A.encrypt(B.get_public_key(), test_msg, aad_msg);

  if(!encr.has_value())
  {
    spdlog::error("ecnr failed");
    return;
  }

  std::cout << "Encr: ";
  StringSource((const byte*)encr.value().cipher_text.data(), encr.value().cipher_text.size(), true, new HexEncoder(new FileSink(std::cout)));
  std::cout << std::endl;

  const auto decr = B.decrypt(A.get_public_key(), encr.value());

  if(!decr.has_value())
  {
    spdlog::error("decr failed");
    return;
  }

  std::cout << "Decr: ";
  StringSource((const byte*)decr.value().cipher_text.data(), decr.value().cipher_text.size(),
               true, new FileSink(std::cout));
  std::cout << "\n" << std::endl;
  std::cout << "aad : " << decr.value().aad << '\n';
}

}
