#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "mfsync/crypto.h"
#include "mfsync/file_handler.h"

namespace mfsync::protocol {

constexpr auto TCP_PORT = 8000;
constexpr auto MULTICAST_PORT = 30001;
constexpr auto MULTICAST_LISTEN_ADDRESS = "0.0.0.0";
constexpr auto MULTICAST_ADDRESS = "239.255.0.1";
constexpr auto MAX_MESSAGE_SIZE = 1024;
constexpr auto CHUNKSIZE = 1024;
constexpr std::string_view MFSYNC_HEADER_BEGIN = "<MFSYNC_HEADER_BEGIN>";
constexpr std::string_view MFSYNC_HEADER_END = "<MFSYNC_HEADER_END>";
constexpr auto MFSYNC_HEADER_SIZE =
    MFSYNC_HEADER_BEGIN.size() + MFSYNC_HEADER_END.size();
constexpr auto MFSYNC_LOG_PREFIX = "";
constexpr auto VERSION = "0.2.0";

constexpr std::string_view create_begin_transmission_message() {
  return "<MFSYNC_HEADER_BEGIN>BEGIN_TRANSMISSION<MFSYNC_HEADER_END>";
}

enum class type {
  NONE = 0,
  DENIED,
  HANDSHAKE,
  FILE_LIST,
  FILE,
};

type get_message_type(const std::string& msg);
std::optional<nlohmann::json> get_json_from_message(const std::string& msg);

std::string wrap_with_header(const std::string& msg);
std::string create_handshake_message(const std::string& public_key, const std::string& salt);
std::string create_file_list_message(const std::string& public_key);
std::string create_file_message(const std::string& public_key,
                                const std::string& msg);
std::string create_error_message(const std::string& reason);

std::string create_message_from_requested_file(const requested_file& file);
std::optional<requested_file> get_requested_file_from_message(
    const std::string& message);

std::optional<host_information> get_host_info_from_message(
    const std::string& message, const boost::asio::ip::udp::endpoint& endpoint);
std::string create_host_announcement_message(const std::string& pub_key,
                                             unsigned short port);

std::string create_message_from_file_info(
    const file_handler::stored_files& file_infos, unsigned short port);
std::vector<std::string> create_messages_from_file_info(
    const file_handler::stored_files& file_infos, unsigned short port);

std::tuple<bool, std::string, crypto::encryption_wrapper> decompose_message(
    const std::string& message);
std::optional<std::string> get_decrypted_message(
    const std::string& message, const std::string& public_key,
    crypto::crypto_handler& handler);
std::optional<std::string> get_decrypted_message(
    const std::string& message, crypto::crypto_handler& handler);

std::optional<size_t> get_count_from_message(const std::string& message);

std::optional<file_handler::available_files> get_available_files_from_message(
    const std::string& message, const boost::asio::ip::tcp::endpoint& endpoint,
    const std::string& pub_key = "");

std::optional<file_handler::available_files> get_available_files_from_message(
    const std::string& message, const boost::asio::ip::udp::endpoint& endpoint,
    const std::string& pub_key = "");

std::optional<file_handler::available_files> get_available_files_from_message(
    const std::string& message, const boost::asio::ip::address& address = {},
    const std::string& pub_key = "");

inline std::string create_denied_message() {
  nlohmann::json j;
  j["type"] = "denied";
  return protocol::wrap_with_header(j.dump());
}

template <typename Result_t>
class converter {};

template <>
class converter<requested_file> {
 public:
  static std::string to_message(const requested_file& requested,
                                const std::string& pub_key,
                                mfsync::crypto::crypto_handler& handler) {
    auto tmp_msg = protocol::create_message_from_requested_file(requested);
    auto wrapper = handler.encrypt(pub_key, tmp_msg);

    if (!wrapper.has_value()) {
      spdlog::debug("encrypt failed for {}", pub_key);
      return protocol::create_denied_message();
    }

    auto j = nlohmann::json(wrapper.value());
    return protocol::create_file_message(handler.get_public_key(), j.dump());
  }

  // returns pair of requested_file and pub_key of sender
  static std::optional<std::pair<requested_file, std::string>> from_message(
      const std::string& buf, mfsync::crypto::crypto_handler& handler) {
    const auto [no_error, pub_key, wrapper] = protocol::decompose_message(buf);
    const auto file_str = protocol::get_decrypted_message(buf, handler);

    if (!file_str.has_value()) {
      spdlog::debug("converter: could not decrypt message");
      return std::nullopt;
    }

    const auto file_j = protocol::get_json_from_message(file_str.value());
    return std::make_pair(file_j.value().get<requested_file>(), pub_key);
  }
};

template <>
class converter<bool> {
 public:
  static std::optional<bool> from_message(
      const std::string& buf, const std::string& pub_key,
      mfsync::crypto::crypto_handler& handler) {
    if (protocol::get_message_type(buf) == protocol::type::DENIED) {
      return std::nullopt;
    }

    auto decrypted_message =
        protocol::get_decrypted_message(buf, pub_key, handler);

    if (!decrypted_message.has_value()) {
      return std::nullopt;
    }

    nlohmann::json j = nlohmann::json::parse(decrypted_message.value());
    if (j.at("type") != "accepted") {
      return false;
    }

    return true;
  }

  static std::string to_message(bool value, const std::string& pub_key,
                                mfsync::crypto::crypto_handler& handler) {
    nlohmann::json j;
    j["type"] = value ? "accepted" : "denied";

    auto wrapped = handler.encrypt(pub_key, j.dump());

    if (!wrapped.has_value()) {
      j["type"] = "denied";
      return protocol::wrap_with_header(j.dump());
    }

    j = nlohmann::json(wrapped.value());
    return protocol::wrap_with_header(j.dump());
  }
};

template <>
class converter<mfsync::file_handler::available_files> {
 public:
  static std::string to_message(mfsync::file_handler& file_handler,
                                unsigned short port, const std::string& pub_key,
                                mfsync::crypto::crypto_handler& handler) {
    std::string result;
    if (!handler.trust_key(pub_key)) {
      return protocol::create_denied_message();
    }

    auto msg = protocol::create_message_from_file_info(
        file_handler.get_stored_files(), port);

    auto wrapper = handler.encrypt(pub_key, msg);

    if (!wrapper.has_value()) {
      spdlog::debug("encrypt failed for {}", pub_key);
      return protocol::create_denied_message();
    }

    auto j = nlohmann::json(wrapper.value());
    return protocol::wrap_with_header(j.dump());
  }

  static std::optional<mfsync::file_handler::available_files> from_message(
      const std::string& buf, const std::string& pub_key,
      mfsync::crypto::crypto_handler& handler,
      const boost::asio::ip::tcp::endpoint& address, bool update_count = false) {
    if (mfsync::protocol::get_message_type({buf.data(), buf.size()}) ==
        mfsync::protocol::type::DENIED) {
      spdlog::debug("file list request got denied by host {}.", pub_key);
      return std::nullopt;
    }

    if(update_count) {
      const auto count = mfsync::protocol::get_count_from_message(buf);
      if(count.has_value()) {
          handler.set_count(pub_key, count.value());
      } else
      {
          spdlog::debug("Could not read count from message");
      }
    }
    const auto decrypted_message =
        mfsync::protocol::get_decrypted_message(buf, pub_key, handler);

    if (!decrypted_message.has_value()) {
      spdlog::debug(
          "Error on handle_read_file_request_response: decryption failed");
      return std::nullopt;
    }

    return mfsync::protocol::get_available_files_from_message(
        decrypted_message.value(), address, pub_key);
  }
};

}  // namespace mfsync::protocol
