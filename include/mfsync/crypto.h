#pragma once

#include <optional>

#include "spdlog/spdlog.h"

#include <cryptopp/chachapoly.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/chachapoly.h>
#include <cryptopp/filters.h>
#include <cryptopp/files.h>
#include <cryptopp/hex.h>
#include <cryptopp/xed25519.h>
#include <cryptopp/osrng.h>
#include <filesystem>

namespace mfsync::crypto
{
  using namespace CryptoPP;
  struct key_wrapper {
    key_wrapper() = default;
    key_wrapper(x25519 ecdh_)
      : ecdh(std::move(ecdh_))
      , private_key(ecdh.PrivateKeyLength())
      , public_key(ecdh.PublicKeyLength())
    {}

    static key_wrapper create(const std::filesystem::path& path)
    {
      key_wrapper result;
      if(std::filesystem::exists(path))
      {
        FileSource fsA{"testA", true};
        result.ecdh.Load(fsA);

        AutoSeededRandomPool prng;
        bool valid = result.ecdh.Validate(prng, 3);
        if(valid == false)
        {
          spdlog::error("Invalid private key");
        }
      }
      else
      {
        FileSink filesinkA("testA");
        result = key_wrapper::create();
        result.ecdh.Save(filesinkA);
      }

      return result;
    }

    static key_wrapper create()
    {
      AutoSeededRandomPool rnd_pool;
      key_wrapper result{x25519{rnd_pool}};
      result.ecdh.GenerateKeyPair(rnd_pool, result.private_key, result.public_key);
      return result;
    }

    std::optional<SecByteBlock> get_shared_secret(SecByteBlock other_public_key)
    {
      SecByteBlock shared_key(ecdh.AgreedValueLength());

      if(!ecdh.Agree(shared_key, private_key, other_public_key))
      {
        spdlog::error("Failed to reach shared secret");
        return std::nullopt;
      }

      return shared_key;
    }

    x25519 ecdh;
    SecByteBlock private_key;
    SecByteBlock public_key;
  };

  struct encryption_wrapper
  {
    static encryption_wrapper create(SecByteBlock secret, std::string plain, size_t count, std::string arbitary_data = "")
    {
      encryption_wrapper result;
      result.cipher_text.resize(plain.size());
      result.aad = std::move(arbitary_data);

      auto IV = get_nonce_from_count(count);

      ChaCha20Poly1305::Encryption enc;
      enc.SetKeyWithIV(secret, secret.size(), IV, IV.size());
      enc.EncryptAndAuthenticate(result.cipher_text.data(),
                                 result.mac.data(),
                                 result.mac.size(),
                                 IV,
                                 IV.size(),
                                 (const byte*)result.aad.data(),
                                 result.aad.size(),
                                 (const byte*)plain.data(),
                                 plain.size());
      return result;
    }

    static SecByteBlock get_nonce_from_count(size_t count)
    {
      SecByteBlock result(reinterpret_cast<const byte*>(&count), sizeof(count));
      result.CleanGrow(12);
      return result;
    }

    static std::optional<encryption_wrapper> decrypt(SecByteBlock secret, const encryption_wrapper& wrapper, size_t count)
    {
      encryption_wrapper result;
      result.cipher_text.resize(wrapper.cipher_text.size());
      result.mac = wrapper.mac;

      auto IV = get_nonce_from_count(count);

      ChaCha20Poly1305::Decryption dec;
      dec.SetKeyWithIV(secret, secret.size(), IV, IV.size());
      if(dec.DecryptAndVerify(result.cipher_text.data(), result.mac.data(), result.mac.size(),
                              IV, IV.size(), (const byte*)wrapper.aad.data(),
                              wrapper.aad.size(), wrapper.cipher_text.data(), wrapper.cipher_text.size()))
      {
        return result;
      }

      return std::nullopt;
    }

    std::vector<byte> cipher_text;
    std::array<byte, 16> mac;
    std::string aad;
  };

  void test();
}
