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

#include <nlohmann/json.hpp>

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
      //somehow ecdh.Load(..) does not initialize the keys
      //therefore get_shared_secret fails later on
      if(false && std::filesystem::exists(path))
      {
        FileSource fsA{path.c_str(), true};
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
        FileSink filesinkA(path.c_str());
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
      result.aad = wrapper.aad;

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

  inline void to_json(nlohmann::json& j, const encryption_wrapper& file_info) {
    //TODO: this is super inefficient!
    j["cipher_text"] = file_info.cipher_text;
    j["mac"] = file_info.mac;
    j["aad"] = file_info.aad;
  }

  inline void from_json(const nlohmann::json& j, encryption_wrapper& file_info) {
    file_info.cipher_text = j.at("cipher_text").get<std::vector<byte>>();
    file_info.mac = j.at("mac").get<std::array<byte, 16>>();
    j.at("aad").get_to(file_info.aad);
  }

  struct key_count_pair
  {
    SecByteBlock key;
    size_t count = 0;
  };

  class crypto_handler
  {
  public:
    bool init(const std::filesystem::path& path)
    {
      std::unique_lock lk{mutex_};
      key_pair_ = key_wrapper::create(path);
      return true;
    }

    std::string get_public_key() const
    {
      std::string result;
      HexEncoder encoder(new StringSink(result));
      StringSource(key_pair_.public_key, key_pair_.public_key.size(),
                   true, new Redirector(encoder));
      return result;
    }

    bool trust_key(std::string pub_key)
    {
      if(!trust_all_)
      {
        //TODO: list with keys that are allowed
        return false;
      }

      std::unique_lock lk{mutex_};
      if(trusted_keys_.contains(pub_key))
      {
        return true;
      }

      SecByteBlock decoded;

      HexDecoder decoder;
      decoder.Put( (byte*)pub_key.data(), pub_key.size() );
      decoder.MessageEnd();
      word64 size = decoder.MaxRetrievable();
      if(size && size <= SIZE_MAX)
      {
          decoded.resize(size);
          decoder.Get((byte*)&decoded[0], decoded.size());
      }


      auto shared_secret = key_pair_.get_shared_secret(std::move(decoded));

      if(!shared_secret.has_value())
      {
        spdlog::debug("Creating shared secret from pub key {} failed", pub_key);
        return false;
      }

      trusted_keys_[pub_key] = key_count_pair{ .key = std::move(shared_secret.value()) };
      return true;
    }

    std::optional<encryption_wrapper> encrypt(const std::string& pub_key,
                                              std::string plain,
                                              std::string aad = "")
    {
      if(!trusted_keys_.contains(pub_key))
      {
        return std::nullopt;
      }

      return encryption_wrapper::create(trusted_keys_.at(pub_key).key,
                                        std::move(plain),
                                        get_count(pub_key),
                                        std::move(aad));
    }

    std::optional<encryption_wrapper> decrypt(const std::string& pub_key,
                                              const encryption_wrapper& wrapper)
    {
      if(!trusted_keys_.contains(pub_key))
      {
        return std::nullopt;
      }

      return encryption_wrapper::decrypt(trusted_keys_.at(pub_key).key,
                                         wrapper,
                                         get_count(pub_key));
    }


  private:
    size_t get_count(const std::string& pub_key)
    {
      std::unique_lock lk{mutex_};
      if(!trusted_keys_.contains(pub_key))
      {
        spdlog::error("get_count of non trusted key.");
        return 0;
      }

      return ++trusted_keys_.at(pub_key).count;
    }

    mutable std::mutex mutex_;
    std::vector<size_t> count_vec_;

    key_wrapper key_pair_;

    bool trust_all_ = true;
    //mapping public key to shared key + nonce count
    std::map<std::string, key_count_pair> trusted_keys_;
  };

  void test();
}
