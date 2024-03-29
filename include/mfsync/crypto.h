#pragma once

#include <cryptopp/chachapoly.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/osrng.h>
#include <cryptopp/xed25519.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>

#include "spdlog/spdlog.h"

namespace mfsync::crypto {
using namespace CryptoPP;

class x25519Wrapper : public x25519 {
 public:
  using x25519::x25519;

  const byte* GetPrivateKey() { return m_sk.begin(); }

  const byte* GetPublicKey() { return m_pk.begin(); }
};

struct key_pair {
  key_pair() = default;
  key_pair(x25519Wrapper ecdh_)
      : ecdh(std::move(ecdh_)),
        private_key(ecdh.PrivateKeyLength()),
        public_key(ecdh.PublicKeyLength()) {}

  static key_pair create(const std::filesystem::path& path);
  static key_pair create();
  std::optional<SecByteBlock> get_shared_secret(SecByteBlock other_public_key, SecByteBlock salt);

  x25519Wrapper ecdh;
  SecByteBlock private_key;
  SecByteBlock public_key;

 private:
  static std::optional<key_pair> load_from_file(
      const std::filesystem::path& path);
  static void save_to_file(const key_pair& key,
                           const std::filesystem::path& path);
};

struct encryption_wrapper {
  static encryption_wrapper create(SecByteBlock secret, std::string plain,
                                   size_t count,
                                   std::string arbitary_data = "");
  static SecByteBlock get_nonce_from_count(size_t count);
  static std::optional<encryption_wrapper> decrypt(
      SecByteBlock secret, const encryption_wrapper& wrapper, size_t count);

  std::vector<byte> cipher_text;
  std::array<byte, 16> mac;
  size_t count;
  std::string aad;
};

struct key_count_pair {
  SecByteBlock key;
  size_t count = 0;
};

class crypto_handler {
 public:
  bool init(const std::filesystem::path& path);
  std::string get_public_key() const;
  std::string encode(SecByteBlock value) const;
  SecByteBlock decode(std::string value) const;
  void add_allowed_key(const std::string& pub_key);
  bool is_allowed(const std::string& pub_key) const;
  bool trust_key(std::string pub_key, std::optional<std::string> salt = std::nullopt);

  std::unique_ptr<crypto_handler> derive(const std::string& pub_key, const std::string& salt);

  SecByteBlock generate_salt() const;

  std::optional<encryption_wrapper> encrypt(const std::string& pub_key,
                                            std::string plain,
                                            std::string aad = "");

  bool EndOfFile(const FileSource& file);

  void encrypt_file_to_buf(const std::string& pub_key, std::ifstream& ifstream,
                           size_t block_size, std::vector<unsigned char>& out);

  void decrypt_file_to_buf(const std::string& pub_key, std::ofstream& ofstream,
                           size_t block_size, std::vector<uint8_t>& in,
                           bool pump_all);

  std::optional<encryption_wrapper> decrypt(const std::string& pub_key,
                                            const encryption_wrapper& wrapper);

  void set_count(const std::string& pub_key, size_t count);

 private:
  size_t get_count(const std::string& pub_key);

  mutable std::mutex mutex_;
  key_pair key_pair_;

  bool trust_all_ = true;
  // mapping public key to shared key + nonce count
  std::map<std::string, key_count_pair> trusted_keys_;
  std::vector<std::string> allowed_keys_;
};

inline void to_json(nlohmann::json& j, const encryption_wrapper& file_info) {
  // TODO: this is super inefficient!
  j["cipher_text"] = file_info.cipher_text;
  j["mac"] = file_info.mac;
  j["count"] = file_info.count;
  j["aad"] = file_info.aad;
}

inline void from_json(const nlohmann::json& j, encryption_wrapper& file_info) {
  file_info.cipher_text = j.at("cipher_text").get<std::vector<byte>>();
  file_info.mac = j.at("mac").get<std::array<byte, 16>>();
  j.at("aad").get_to(file_info.aad);
  j.at("count").get_to(file_info.count);
}

}  // namespace mfsync::crypto
