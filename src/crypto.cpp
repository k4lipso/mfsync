#include "mfsync/crypto.h"

#include <cryptopp/chachapoly.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include <cryptopp/osrng.h>
#include <cryptopp/xed25519.h>

#include "spdlog/spdlog.h"

namespace mfsync::crypto {
key_wrapper key_wrapper::create(const std::filesystem::path& path) {
  key_wrapper result;
  // somehow ecdh.Load(..) does not initialize the keys
  // therefore get_shared_secret fails later on
  if (std::filesystem::exists(path)) {
    FileSource fsA{path.c_str(), true};
    result.ecdh.Load(fsA);

    AutoSeededRandomPool prng;
    bool valid = result.ecdh.Validate(prng, 3);
    if (valid == false) {
      spdlog::error("Invalid private key");
    }

    result.private_key.Assign(result.ecdh.GetPrivateKey(),
                              result.ecdh.PrivateKeyLength());

    result.public_key.Assign(result.ecdh.GetPublicKey(),
                             result.ecdh.PublicKeyLength());

    // result.public_key.Assign(
    //     dynamic_cast<ed25519PrivateKey*>(&result.ecdh)->GetPublicKeyBytePtr(),
    //     result.ecdh.PublicKeyLength());
    //  result.private_key =
    //  dynamic_cast<ed25519PrivateKey*>(&result.ecdh)->GetPrivateKeyBytePtr();
    //  result.public_key =
    //  dynamic_cast<ed25519PrivateKey*>(&result.ecdh)->GetPublicKeyBytePtr();

  } else {
    FileSink filesinkA(path.c_str());
    result = key_wrapper::create();

    result.private_key.Assign(result.ecdh.GetPrivateKey(),
                              result.ecdh.PrivateKeyLength());

    result.public_key.Assign(result.ecdh.GetPublicKey(),
                             result.ecdh.PublicKeyLength());

    result.ecdh.Save(filesinkA);
  }

  return result;
}

key_wrapper key_wrapper::create() {
  AutoSeededRandomPool rnd_pool;
  key_wrapper result{x25519Wrapper{rnd_pool}};
  result.ecdh.GenerateKeyPair(rnd_pool, result.private_key, result.public_key);
  return result;
}

std::optional<SecByteBlock> key_wrapper::get_shared_secret(
    SecByteBlock other_public_key) {
  SecByteBlock shared_key(ecdh.AgreedValueLength());

  if (!ecdh.Agree(shared_key, private_key, other_public_key)) {
    spdlog::error("Failed to reach shared secret");
    return std::nullopt;
  }

  return shared_key;
}

encryption_wrapper encryption_wrapper::create(
    SecByteBlock secret, std::string plain, size_t count,
    std::string arbitary_data /*= "" */) {
  encryption_wrapper result;
  result.cipher_text.resize(plain.size());
  result.aad = std::move(arbitary_data);

  auto IV = get_nonce_from_count(count);

  ChaCha20Poly1305::Encryption enc;
  enc.SetKeyWithIV(secret, secret.size(), IV, IV.size());
  enc.EncryptAndAuthenticate(result.cipher_text.data(), result.mac.data(),
                             result.mac.size(), IV, IV.size(),
                             (const byte*)result.aad.data(), result.aad.size(),
                             (const byte*)plain.data(), plain.size());
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
          IV.size(), (const byte*)wrapper.aad.data(), wrapper.aad.size(),
          wrapper.cipher_text.data(), wrapper.cipher_text.size())) {
    return result;
  }

  return std::nullopt;
}

bool crypto_handler::init(const std::filesystem::path& path) {
  std::unique_lock lk{mutex_};
  key_pair_ = key_wrapper::create(path);
  return true;
}

std::string crypto_handler::get_public_key() const {
  std::string result;
  HexEncoder encoder(new StringSink(result));
  StringSource(key_pair_.public_key, key_pair_.public_key.size(), true,
               new Redirector(encoder));
  return result;
}

void crypto_handler::add_allowed_key(const std::string& pub_key) {
  allowed_keys_.push_back(std::move(pub_key));
}

bool crypto_handler::trust_key(std::string pub_key) {
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

  SecByteBlock decoded;

  HexDecoder decoder;
  decoder.Put((byte*)pub_key.data(), pub_key.size());
  decoder.MessageEnd();
  word64 size = decoder.MaxRetrievable();
  if (size && size <= SIZE_MAX) {
    decoded.resize(size);
    decoder.Get((byte*)&decoded[0], decoded.size());
  }

  auto shared_secret = key_pair_.get_shared_secret(std::move(decoded));

  if (!shared_secret.has_value()) {
    spdlog::debug("Creating shared secret from pub key {} failed", pub_key);
    return false;
  }

  trusted_keys_[pub_key] =
      key_count_pair{.key = std::move(shared_secret.value())};
  return true;
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
  // void encrypt_file(const std::string& pub_key, std::string file_name)
  // {
  //  encryption_wrapper result;
  //  result.cipher_text.resize(plain.size());
  //  result.aad = std::move(arbitary_data);

  const int TAG_SIZE = -1;
  static auto IV = encryption_wrapper::get_nonce_from_count(get_count(pub_key));

  auto shared = trusted_keys_.at(pub_key);
  ChaCha20Poly1305::Encryption enc;
  enc.SetKeyWithIV(shared.key, shared.key.size(), IV, IV.size());

  // todo: may needs 'new' and then filter takes care of delete
  std::vector<unsigned char> v;
  CryptoPP::FileSource source(ifstream, false);
  // CryptoPP::FileSource source(ifstream, false);
  CryptoPP::MeterFilter meter;
  CryptoPP::AuthenticatedEncryptionFilter filter(enc, nullptr, false, TAG_SIZE);
  CryptoPP::VectorSink sink(out);
  // CryptoPP::FileSink sink(std::string(file_name +
  // ".encrypted").c_str());

  source.Attach(new Redirector(filter));
  filter.Attach(new Redirector(meter));
  meter.Attach(new Redirector(sink));

  lword processed = 0;

  // while (!EndOfFile(source) && !source.SourceExhausted()) {
  source.Pump(block_size);

  if (out.size() < block_size) {
    filter.Flush(true);
  } else {
    filter.Flush(false);
  }

  processed += block_size;

  if (processed % (1024 * 1024 * 10) == 0)
    std::cout << "Processed: " << meter.GetTotalBytes() << std::endl;
  //}

  // todo;: maybe needed on eof
  // filter.MessageEnd();
}

void crypto_handler::decrypt_file_to_buf(const std::string& pub_key,
                                         std::ofstream& ofstream,
                                         size_t block_size,
                                         std::vector<uint8_t>& in,
                                         bool pump_all) {
  // encryption_wrapper result;
  // result.cipher_text.resize(plain.size());
  // result.aad = std::move(arbitary_data);

  const int TAG_SIZE = -1;
  static auto IV = encryption_wrapper::get_nonce_from_count(get_count(pub_key));

  auto shared = trusted_keys_.at(pub_key);

  ChaCha20Poly1305::Decryption dec;
  dec.SetKeyWithIV(shared.key, shared.key.size(), IV, IV.size());

  in.resize(block_size);
  // todo: may needs 'new' and then filter takes care of delete
  // CryptoPP::FileSource source(file_name.c_str(), false);
  CryptoPP::VectorSource source(in, false);
  CryptoPP::MeterFilter meter;
  CryptoPP::AuthenticatedDecryptionFilter filter(
      dec, nullptr, AuthenticatedDecryptionFilter::DEFAULT_FLAGS, TAG_SIZE);
  CryptoPP::FileSink sink(ofstream);

  source.Attach(new Redirector(filter));
  filter.Attach(new Redirector(meter));
  meter.Attach(new Redirector(sink));

  lword processed = 0;

  // while (!EndOfFile(source) && !source.SourceExhausted()) {
  // source.Pump(block_size - TAG_SIZE);
  if (pump_all) {
    source.PumpAll();
    [[maybe_unused]] bool b = filter.GetLastResult();
  } else {
    source.Pump(block_size);
  }
  filter.Flush(true);

  processed += block_size;

  if (processed % (1024 * 1024 * 10) == 0)
    std::cout << "Processed: " << meter.GetTotalBytes() << std::endl;
  // sink.Flush(true);
  //}

  // filter.MessageEnd();
}

std::optional<encryption_wrapper> crypto_handler::decrypt(
    const std::string& pub_key, const encryption_wrapper& wrapper) {
  if (!trusted_keys_.contains(pub_key)) {
    return std::nullopt;
  }

  return encryption_wrapper::decrypt(trusted_keys_.at(pub_key).key, wrapper,
                                     get_count(pub_key));
}

size_t crypto_handler::get_count(const std::string& pub_key) {
  std::unique_lock lk{mutex_};
  if (!trusted_keys_.contains(pub_key)) {
    spdlog::error("get_count of non trusted key.");
    return 0;
  }

  // return ++trusted_keys_.at(pub_key).count;
  return trusted_keys_.at(pub_key).count;
}
}  // namespace mfsync::crypto
