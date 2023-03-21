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
struct key_wrapper {
    key_wrapper() = default;
    key_wrapper(x25519 ecdh_)
        : ecdh(std::move(ecdh_)),
          private_key(ecdh.PrivateKeyLength()),
          public_key(ecdh.PublicKeyLength()) {}

    static key_wrapper create(const std::filesystem::path& path) {
        key_wrapper result;
        // somehow ecdh.Load(..) does not initialize the keys
        // therefore get_shared_secret fails later on
        if (false && std::filesystem::exists(path)) {
            FileSource fsA{path.c_str(), true};
            result.ecdh.Load(fsA);

            AutoSeededRandomPool prng;
            bool valid = result.ecdh.Validate(prng, 3);
            if (valid == false) {
                spdlog::error("Invalid private key");
            }
        } else {
            FileSink filesinkA(path.c_str());
            result = key_wrapper::create();
            result.ecdh.Save(filesinkA);
        }

        return result;
    }

    static key_wrapper create() {
        AutoSeededRandomPool rnd_pool;
        key_wrapper result{x25519{rnd_pool}};
        result.ecdh.GenerateKeyPair(rnd_pool, result.private_key,
                                    result.public_key);
        return result;
    }

    std::optional<SecByteBlock> get_shared_secret(
        SecByteBlock other_public_key) {
        SecByteBlock shared_key(ecdh.AgreedValueLength());

        if (!ecdh.Agree(shared_key, private_key, other_public_key)) {
            spdlog::error("Failed to reach shared secret");
            return std::nullopt;
        }

        return shared_key;
    }

    x25519 ecdh;
    SecByteBlock private_key;
    SecByteBlock public_key;
};

struct encryption_wrapper {
    static encryption_wrapper create(SecByteBlock secret, std::string plain,
                                     size_t count,
                                     std::string arbitary_data = "") {
        encryption_wrapper result;
        result.cipher_text.resize(plain.size());
        result.aad = std::move(arbitary_data);

        auto IV = get_nonce_from_count(count);

        ChaCha20Poly1305::Encryption enc;
        enc.SetKeyWithIV(secret, secret.size(), IV, IV.size());
        enc.EncryptAndAuthenticate(
            result.cipher_text.data(), result.mac.data(), result.mac.size(), IV,
            IV.size(), (const byte*)result.aad.data(), result.aad.size(),
            (const byte*)plain.data(), plain.size());
        return result;
    }

    static SecByteBlock get_nonce_from_count(size_t count) {
        SecByteBlock result(reinterpret_cast<const byte*>(&count),
                            sizeof(count));
        result.CleanGrow(12);
        return result;
    }

    static std::optional<encryption_wrapper> decrypt(
        SecByteBlock secret, const encryption_wrapper& wrapper, size_t count) {
        encryption_wrapper result;
        result.cipher_text.resize(wrapper.cipher_text.size());
        result.mac = wrapper.mac;
        result.aad = wrapper.aad;

        auto IV = get_nonce_from_count(count);

        ChaCha20Poly1305::Decryption dec;
        dec.SetKeyWithIV(secret, secret.size(), IV, IV.size());
        if (dec.DecryptAndVerify(result.cipher_text.data(), result.mac.data(),
                                 result.mac.size(), IV, IV.size(),
                                 (const byte*)wrapper.aad.data(),
                                 wrapper.aad.size(), wrapper.cipher_text.data(),
                                 wrapper.cipher_text.size())) {
            return result;
        }

        return std::nullopt;
    }

    std::vector<byte> cipher_text;
    std::array<byte, 16> mac;
    std::string aad;
};

inline void to_json(nlohmann::json& j, const encryption_wrapper& file_info) {
    // TODO: this is super inefficient!
    j["cipher_text"] = file_info.cipher_text;
    j["mac"] = file_info.mac;
    j["aad"] = file_info.aad;
}

inline void from_json(const nlohmann::json& j, encryption_wrapper& file_info) {
    file_info.cipher_text = j.at("cipher_text").get<std::vector<byte>>();
    file_info.mac = j.at("mac").get<std::array<byte, 16>>();
    j.at("aad").get_to(file_info.aad);
}

struct key_count_pair {
    SecByteBlock key;
    size_t count = 0;
};

class crypto_handler {
   public:
    bool init(const std::filesystem::path& path) {
        std::unique_lock lk{mutex_};
        key_pair_ = key_wrapper::create(path);
        return true;
    }

    std::string get_public_key() const {
        std::string result;
        HexEncoder encoder(new StringSink(result));
        StringSource(key_pair_.public_key, key_pair_.public_key.size(), true,
                     new Redirector(encoder));
        return result;
    }

    void add_allowed_key(const std::string& pub_key) {
        allowed_keys_.push_back(std::move(pub_key));
    }

    bool trust_key(std::string pub_key) {
        if (!allowed_keys_.empty()) {
            if (std::none_of(
                    allowed_keys_.begin(), allowed_keys_.end(),
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
            spdlog::debug("Creating shared secret from pub key {} failed",
                          pub_key);
            return false;
        }

        trusted_keys_[pub_key] =
            key_count_pair{.key = std::move(shared_secret.value())};
        return true;
    }

    std::optional<encryption_wrapper> encrypt(const std::string& pub_key,
                                              std::string plain,
                                              std::string aad = "") {
        if (!trusted_keys_.contains(pub_key)) {
            return std::nullopt;
        }

        return encryption_wrapper::create(trusted_keys_.at(pub_key).key,
                                          std::move(plain), get_count(pub_key),
                                          std::move(aad));
    }

    bool EndOfFile(const FileSource& file) {
        std::istream* stream = const_cast<FileSource&>(file).GetStream();
        return stream->eof();
    }

    void encrypt_file_to_buf(const std::string& pub_key,
                             std::ifstream& ifstream, size_t block_size,
                             std::vector<unsigned char>& out) {
        // void encrypt_file(const std::string& pub_key, std::string file_name)
        // {
        //  encryption_wrapper result;
        //  result.cipher_text.resize(plain.size());
        //  result.aad = std::move(arbitary_data);

        const int TAG_SIZE = -1;
        static auto IV =
            encryption_wrapper::get_nonce_from_count(get_count(pub_key));

        auto shared = trusted_keys_.at(pub_key);
        ChaCha20Poly1305::Encryption enc;
        enc.SetKeyWithIV(shared.key, shared.key.size(), IV, IV.size());

        // todo: may needs 'new' and then filter takes care of delete
        std::vector<unsigned char> v;
        CryptoPP::FileSource source(ifstream, false);
        // CryptoPP::FileSource source(ifstream, false);
        CryptoPP::MeterFilter meter;
        CryptoPP::AuthenticatedEncryptionFilter filter(enc, nullptr, false,
                                                       TAG_SIZE);
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

    void decrypt_file_to_buf(const std::string& pub_key,
                             std::ofstream& ofstream, size_t block_size,
                             std::vector<uint8_t>& in, bool pump_all) {
        // encryption_wrapper result;
        // result.cipher_text.resize(plain.size());
        // result.aad = std::move(arbitary_data);

        const int TAG_SIZE = -1;
        static auto IV =
            encryption_wrapper::get_nonce_from_count(get_count(pub_key));

        auto shared = trusted_keys_.at(pub_key);

        ChaCha20Poly1305::Decryption dec;
        dec.SetKeyWithIV(shared.key, shared.key.size(), IV, IV.size());

        in.resize(block_size);
        // todo: may needs 'new' and then filter takes care of delete
        // CryptoPP::FileSource source(file_name.c_str(), false);
        CryptoPP::VectorSource source(in, false);
        CryptoPP::MeterFilter meter;
        CryptoPP::AuthenticatedDecryptionFilter filter(
            dec, nullptr, AuthenticatedDecryptionFilter::DEFAULT_FLAGS,
            TAG_SIZE);
        CryptoPP::FileSink sink(ofstream);

        source.Attach(new Redirector(filter));
        filter.Attach(new Redirector(meter));
        meter.Attach(new Redirector(sink));

        lword processed = 0;

        // while (!EndOfFile(source) && !source.SourceExhausted()) {
        // source.Pump(block_size - TAG_SIZE);
        if (pump_all) {
            source.PumpAll();
            bool b = filter.GetLastResult();
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

    std::optional<encryption_wrapper> decrypt(
        const std::string& pub_key, const encryption_wrapper& wrapper) {
        if (!trusted_keys_.contains(pub_key)) {
            return std::nullopt;
        }

        return encryption_wrapper::decrypt(trusted_keys_.at(pub_key).key,
                                           wrapper, get_count(pub_key));
    }

   private:
    size_t get_count(const std::string& pub_key) {
        std::unique_lock lk{mutex_};
        if (!trusted_keys_.contains(pub_key)) {
            spdlog::error("get_count of non trusted key.");
            return 0;
        }

        return ++trusted_keys_.at(pub_key).count;
    }

    mutable std::mutex mutex_;
    std::vector<size_t> count_vec_;

    key_wrapper key_pair_;

    bool trust_all_ = true;
    // mapping public key to shared key + nonce count
    std::map<std::string, key_count_pair> trusted_keys_;
    std::vector<std::string> allowed_keys_;
};

void test();
}  // namespace mfsync::crypto
