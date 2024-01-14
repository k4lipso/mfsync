#include "mfsync/crypto.h"

#include <cryptopp/chachapoly.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/osrng.h>
#include <cryptopp/xed25519.h>
#include <cryptopp/hkdf.h>
#include <cryptopp/sha.h>

#include "spdlog/spdlog.h"

namespace mfsync::crypto {
key_pair key_pair::create(const std::filesystem::path& path) {
  const auto loaded_key = load_from_file(path);

  if (loaded_key.has_value()) {
    return loaded_key.value();
  }

  auto result = key_pair::create();
  result.private_key.Assign(result.ecdh.GetPrivateKey(),
                            result.ecdh.PrivateKeyLength());
  result.public_key.Assign(result.ecdh.GetPublicKey(),
                           result.ecdh.PublicKeyLength());
  save_to_file(result, path);

  return result;
}

std::optional<key_pair> key_pair::load_from_file(
    const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }

  FileSource fsA{path.c_str(), true};

  key_pair result;

  // somehow ecdh.Load(..) does not initialize the keys
  // therefore get_shared_secret fails later on
  result.ecdh.Load(fsA);

  AutoSeededRandomPool prng;
  bool valid = result.ecdh.Validate(prng, 3);
  if (valid == false) {
    spdlog::error("Invalid private key");
    return std::nullopt;
  }

  result.private_key.Assign(result.ecdh.GetPrivateKey(),
                            result.ecdh.PrivateKeyLength());

  result.public_key.Assign(result.ecdh.GetPublicKey(),
                           result.ecdh.PublicKeyLength());

  return result;
}

void key_pair::save_to_file(const key_pair& key,
                            const std::filesystem::path& path) {
  FileSink filesinkA(path.c_str());
  key.ecdh.Save(filesinkA);
}

key_pair key_pair::create() {
  AutoSeededRandomPool rnd_pool;
  key_pair result{x25519Wrapper{rnd_pool}};
  result.ecdh.GenerateKeyPair(rnd_pool, result.private_key, result.public_key);
  return result;
}

std::optional<SecByteBlock> key_pair::get_shared_secret(
    SecByteBlock other_public_key, SecByteBlock salt) {
  SecByteBlock shared_key(ecdh.AgreedValueLength());

  if (!ecdh.Agree(shared_key, private_key, other_public_key)) {
    spdlog::error("Failed to reach shared secret");
    return std::nullopt;
  }

  if(&salt[0] == nullptr) {
    return std::nullopt;
  }

  HKDF<SHA256> hkdf{};
  SecByteBlock derived;
  derived.resize(SHA256::DIGESTSIZE);
  byte info[] = "KeyDerivation";
  hkdf.DeriveKey((byte*)&derived[0], derived.size(), shared_key, sizeof(shared_key), (byte*)&salt[0], salt.size(), info, strlen((const char*)info));

  return derived;
}

encryption_wrapper encryption_wrapper::create(
    SecByteBlock secret, std::string plain, size_t count,
    std::string arbitary_data /*= "" */) {
  encryption_wrapper result;
  result.count = count;
  result.cipher_text.resize(plain.size());
  result.aad = std::move(arbitary_data);

  auto IV = get_nonce_from_count(count);

  ChaCha20Poly1305::Encryption enc;
  enc.SetKeyWithIV(secret, secret.size(), IV, IV.size());
  enc.EncryptAndAuthenticate(result.cipher_text.data(), result.mac.data(),
                             result.mac.size(), IV, IV.size(),
                             reinterpret_cast<const byte*>(result.aad.data()), result.aad.size(),
                             reinterpret_cast<const byte*>(plain.data()), plain.size());
  return result;
}

SecByteBlock encryption_wrapper::get_nonce_from_count(size_t count) {
  SecByteBlock result(reinterpret_cast<const byte*>(&count), sizeof(count));
  result.CleanGrow(12);
  return result;
}

std::optional<encryption_wrapper> encryption_wrapper::decrypt(
    SecByteBlock secret, const encryption_wrapper& wrapper, size_t count) {
  encryption_wrapper result;
  result.cipher_text.resize(wrapper.cipher_text.size());
  result.mac = wrapper.mac;
  result.aad = wrapper.aad;

  auto IV = get_nonce_from_count(count);

  ChaCha20Poly1305::Decryption dec;
  dec.SetKeyWithIV(secret, secret.size(), IV, IV.size());
  if (dec.DecryptAndVerify(
          result.cipher_text.data(), result.mac.data(), result.mac.size(), IV,
          IV.size(), reinterpret_cast<const byte*>(wrapper.aad.data()), wrapper.aad.size(),
          wrapper.cipher_text.data(), wrapper.cipher_text.size())) {
    return result;
  }

  return std::nullopt;
}

bool crypto_handler::init(const std::filesystem::path& path) {
  std::unique_lock lk{mutex_};
  key_pair_ = key_pair::create(path);
  return true;
}

std::string crypto_handler::get_public_key() const {
  return encode(key_pair_.public_key);
}

std::string crypto_handler::encode(SecByteBlock value) const {
  std::string result;
  HexEncoder encoder(new StringSink(result));
  StringSource(value, value.size(), true,
               new Redirector(encoder));
  return result;
}

SecByteBlock crypto_handler::decode(std::string value) const {
  SecByteBlock decoded;

  HexDecoder decoder;
  decoder.Put((byte*)value.data(), value.size());
  decoder.MessageEnd();
  word64 size = decoder.MaxRetrievable();
  if (size && size <= SIZE_MAX) {
    decoded.resize(size);
    decoder.Get((byte*)&decoded[0], decoded.size());
  }

  return decoded;
}


void crypto_handler::add_allowed_key(const std::string& pub_key) {
  allowed_keys_.push_back(std::move(pub_key));
}

bool crypto_handler::trust_key(std::string pub_key, std::optional<std::string> salt /* = std::nullopt */) {
  if (!allowed_keys_.empty()) {
    if (std::none_of(allowed_keys_.begin(), allowed_keys_.end(),
                     [&pub_key](const auto& key) { return key == pub_key; })) {
      return false;
    }
  }

  std::unique_lock lk{mutex_};
  if (trusted_keys_.contains(pub_key)) {
    return true;
  }

  if(!salt.has_value()) {
    spdlog::debug("trust_key: No salt was given...");
    return false;
  }

  auto shared_secret = key_pair_.get_shared_secret(decode(pub_key), decode(salt.value()));

  if (!shared_secret.has_value()) {
    spdlog::debug("Creating shared secret from pub key {} failed", pub_key);
    return false;
  }

  trusted_keys_[pub_key] =
      key_count_pair{.key = std::move(shared_secret.value())};
  return true;
}

std::unique_ptr<crypto_handler> crypto_handler::derive(const std::string& pub_key, const std::string& salt) {
  auto result = std::make_unique<crypto_handler>();
  result->key_pair_ = key_pair_;
  result->trust_all_ = trust_all_;
  result->allowed_keys_ = allowed_keys_;

  spdlog::trace("Derive key: {}, salt: {}", pub_key, salt);
  result->trust_key(pub_key, salt);
  return result;
}

SecByteBlock crypto_handler::generate_salt() const {
  const unsigned int BLOCKSIZE = 16 * 8;
  SecByteBlock salt( BLOCKSIZE );

  CryptoPP::AutoSeededRandomPool rng;
  rng.GenerateBlock( salt, salt.size() );
  return salt;
}

std::optional<encryption_wrapper> crypto_handler::encrypt(
    const std::string& pub_key, std::string plain, std::string aad /*= "" */) {
  if (!trusted_keys_.contains(pub_key)) {
    return std::nullopt;
  }

  return encryption_wrapper::create(trusted_keys_.at(pub_key).key,
                                    std::move(plain), get_count(pub_key),
                                    std::move(aad));
}

bool crypto_handler::EndOfFile(const FileSource& file) {
  std::istream* stream = const_cast<FileSource&>(file).GetStream();
  return stream->eof();
}

void crypto_handler::encrypt_file_to_buf(const std::string& pub_key,
                                         std::ifstream& ifstream,
                                         size_t block_size,
                                         std::vector<unsigned char>& out) {
  if (!trusted_keys_.contains(pub_key)) {
    spdlog::debug("Tried encrypting file to buf with non trusted pub key");
    return;
  }

  const int TAG_SIZE = -1;
  static auto IV = encryption_wrapper::get_nonce_from_count(get_count(pub_key));

  auto shared = trusted_keys_.at(pub_key);
  ChaCha20Poly1305::Encryption enc;
  enc.SetKeyWithIV(shared.key, shared.key.size(), IV, IV.size());

  std::vector<unsigned char> v;
  CryptoPP::FileSource source(ifstream, false);
  CryptoPP::MeterFilter meter;
  CryptoPP::AuthenticatedEncryptionFilter filter(enc, nullptr, false, TAG_SIZE);
  CryptoPP::VectorSink sink(out);

  source.Attach(new Redirector(filter));
  filter.Attach(new Redirector(meter));
  meter.Attach(new Redirector(sink));

  source.Pump(block_size);

  if (out.size() < block_size) {
    filter.Flush(true);
  } else {
    filter.Flush(false);
  }
  // todo;: maybe needed on eof
  // filter.MessageEnd();
  filter.Flush((out.size() < block_size));
}

void crypto_handler::decrypt_file_to_buf(const std::string& pub_key,
                                         std::ofstream& ofstream,
                                         size_t block_size,
                                         std::vector<uint8_t>& in,
                                         bool pump_all) {
  if (!trusted_keys_.contains(pub_key)) {
    spdlog::debug("Tried decrypting file to buf with non trusted pub key");
    return;
  }

  static auto IV = encryption_wrapper::get_nonce_from_count(get_count(pub_key));

  auto shared = trusted_keys_.at(pub_key);

  ChaCha20Poly1305::Decryption dec;
  dec.SetKeyWithIV(shared.key, shared.key.size(), IV, IV.size());

  in.resize(block_size);
  CryptoPP::VectorSource source(in, false);
  CryptoPP::MeterFilter meter;
  CryptoPP::AuthenticatedDecryptionFilter filter(
      dec, nullptr, AuthenticatedDecryptionFilter::DEFAULT_FLAGS);
  CryptoPP::FileSink sink(ofstream);

  source.Attach(new Redirector(filter));
  filter.Attach(new Redirector(meter));
  meter.Attach(new Redirector(sink));

  if (pump_all) {
    source.PumpAll();
  } else {
    source.Pump(block_size);
  }

  filter.Flush(true);
}

std::optional<encryption_wrapper> crypto_handler::decrypt(
    const std::string& pub_key, const encryption_wrapper& wrapper) {
  if (!trusted_keys_.contains(pub_key)) {
    return std::nullopt;
  }

  return encryption_wrapper::decrypt(trusted_keys_.at(pub_key).key, wrapper,
                                     get_count(pub_key));
}

void crypto_handler::set_count(const std::string& pub_key, size_t count) {
  std::unique_lock lk{mutex_};
  if (!trusted_keys_.contains(pub_key)) {
    spdlog::error("set_count of non trusted key.");
    return;
  }

  trusted_keys_.at(pub_key).count = count;
}


size_t crypto_handler::get_count(const std::string& pub_key) {
  std::unique_lock lk{mutex_};
  if (!trusted_keys_.contains(pub_key)) {
    spdlog::error("get_count of non trusted key.");
    return 0;
  }

  return trusted_keys_.at(pub_key).count++;
}
}  // namespace mfsync::crypto
