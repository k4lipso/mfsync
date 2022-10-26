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

  auto keyA = key_wrapper::create();
  auto keyB = key_wrapper::create();

  AutoSeededRandomPool rndA, rndB;
  SecByteBlock IV_A(12), IV_B(12);

	rndA.GenerateBlock(IV_A, IV_A.size());
	rndB.GenerateBlock(IV_B, IV_B.size());

	//25519 ecdhA(rndA), ecdhB(rndB);

	////////////////////////////////////////////////////////////////

  //SecByteBlock privA(ecdhA.PrivateKeyLength());
  //SecByteBlock pubA(ecdhA.PublicKeyLength());
  //ecdhA.GenerateKeyPair(rndA, privA, pubA);

  //SecByteBlock privB(ecdhB.PrivateKeyLength());
  //SecByteBlock pubB(ecdhB.PublicKeyLength());
  //ecdhB.GenerateKeyPair(rndB, privB, pubB);

  //////////////////////////////////////////////////////////////

  auto sharedA = keyA.get_shared_secret(keyB.public_key).value();
  auto sharedB = keyB.get_shared_secret(keyA.public_key).value();

  //SecByteBlock sharedA(ecdhA.AgreedValueLength());
  //SecByteBlock sharedB(ecdhB.AgreedValueLength());

  //if(ecdhA.AgreedValueLength() != ecdhB.AgreedValueLength())
  //    throw std::runtime_error("Shared secret size mismatch");

  //if(!ecdhA.Agree(sharedA, privA, pubB))
  //    throw std::runtime_error("Failed to reach shared secret (1)");

  //if(!ecdhB.Agree(sharedB, privB, pubA))
  //    throw std::runtime_error("Failed to reach shared secret (2)");

  //size_t len = std::min(ecdhA.AgreedValueLength(), ecdhB.AgreedValueLength());
  //if(!len || !VerifyBufsEqual(sharedA.BytePtr(), sharedB.BytePtr(), len))
  //    throw std::runtime_error("Failed to reach shared secret (3)");

  //////////////////////////////////////////////////////////////

  HexEncoder encoder(new FileSink(std::cout));

  std::cout << "Shared secret (A): ";
  StringSource(sharedA, sharedA.size(), true, new Redirector(encoder));
  std::cout << std::endl;

  std::cout << "Shared secret (B): ";
  StringSource(sharedB, sharedB.size(), true, new Redirector(encoder));
  std::cout << std::endl;


  std::string plain("My Plaintext!! My Dear plaintext!!"), cipher, recover;

  auto encrypted = encryption_wrapper::create(sharedA, plain, 2342);

  byte ct[plain.size()], rt[sizeof(ct)], mac[16];

  std::string aad{"arbitary athenticated data unecrypted lul"};
  std::string aad2{"arbitary athenticated data unecrypted lul"};

  ChaCha20Poly1305::Encryption enc;
  enc.SetKeyWithIV(sharedA, sharedA.size(), IV_A, IV_A.size());
  enc.EncryptAndAuthenticate(ct, mac, sizeof(mac), IV_A, IV_A.size(), (const byte*)aad.data(), aad.size(), (const byte*)plain.data(), plain.size());

  std::cout << "Plain: ";
  StringSource((const byte*)plain.data(), plain.size(), true, new HexEncoder(new FileSink(std::cout)));
  std::cout << "\n" << std::endl;

  //std::cout << "Cipher: ";
  //StringSource(ct, sizeof(ct), true, new HexEncoder(new FileSink(std::cout)));
  //std::cout << std::endl;

  //std::cout << "MAC: ";
  //StringSource(mac, sizeof(mac), true, new HexEncoder(new FileSink(std::cout)));
  //std::cout << "\n" << std::endl;

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
  ChaCha20Poly1305::Decryption dec;
  dec.SetKeyWithIV(sharedA, sharedA.size(), IV_B, IV_B.size());
  if(dec.DecryptAndVerify(rt, mac, sizeof(mac), IV_B, IV_B.size(), (const byte*)aad2.data(), aad2.size(), ct, sizeof(ct)))
  {
    std::cout << "Recover: ";
    StringSource(rt, sizeof(rt), true, new HexEncoder(new FileSink(std::cout)));
    std::cout << "\n" << std::endl;
  }

}

}
