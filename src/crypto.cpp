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

  auto keyA = key_wrapper::create("testA");
  auto keyB = key_wrapper::create("testA");

  auto sharedA = keyA.get_shared_secret(keyB.public_key).value();
  auto sharedB = keyB.get_shared_secret(keyA.public_key).value();

  HexEncoder encoder(new FileSink(std::cout));
  std::cout << "Shared secret (A): ";
  StringSource(sharedA, sharedA.size(), true, new Redirector(encoder));
  std::cout << std::endl;
  std::cout << "Shared secret (B): ";
  StringSource(sharedB, sharedB.size(), true, new Redirector(encoder));
  std::cout << std::endl;


  std::string plain("My Plaintext!! My Dear plaintext!!"), cipher, recover;
  auto encrypted = encryption_wrapper::create(sharedA, plain, 2342);

  std::cout << "Plain: ";
  StringSource((const byte*)plain.data(), plain.size(), true, new HexEncoder(new FileSink(std::cout)));
  std::cout << "\n" << std::endl;
  std::cout << "Cipher: ";
  StringSource((const byte*)encrypted.cipher_text.data(), encrypted.cipher_text.size(), true, new HexEncoder(new FileSink(std::cout)));
  std::cout << std::endl;
  std::cout << "MAC: ";
  StringSource(encrypted.mac.data(), encrypted.mac.size(), true, new HexEncoder(new FileSink(std::cout)));
  std::cout << "\n" << std::endl;

  auto decrypted = encryption_wrapper::decrypt(sharedB, encrypted, 2342);
  if(decrypted.has_value())
  {
    std::cout << "Recover: ";
    StringSource((const byte*)decrypted.value().cipher_text.data(), decrypted.value().cipher_text.size(),
                 true, new HexEncoder(new FileSink(std::cout)));
    std::cout << "\n" << std::endl;
  }
}

}
