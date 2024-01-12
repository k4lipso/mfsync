#include "mfsync/protocol.h"

#include <functional>
#include <sstream>
#include <nlohmann/json.hpp>

#include "boost/lexical_cast.hpp"

#include "spdlog/spdlog.h"

namespace mfsync::protocol
{

type get_message_type(const std::string& msg)
{
  if(msg.size() < MFSYNC_HEADER_SIZE)
  {
    spdlog::debug("get_requested_file_from_message on empty message");
    return type::NONE;
  }

  std::string_view view{msg};
  view.remove_prefix(MFSYNC_HEADER_BEGIN.size());
  view.remove_suffix(view.size() - view.find(MFSYNC_HEADER_END));

  try
  {
    auto j = nlohmann::json::parse(view);
    if(!j.contains("type"))
    {
      return type::NONE;
    }

    const auto& type_string = j.at("type").get<std::string>();
    if(type_string == "file_list")
    {
      return type::FILE_LIST;
    }
    if(type_string == "denied")
    {
      return type::DENIED;
    }
    if(type_string == "file")
    {
      return type::FILE;
    }
  }
  catch(std::exception& er)
  {
    spdlog::debug("Json Error: {}", er.what());
  }

  return type::NONE;
}

std::optional<nlohmann::json> get_json_from_message(const std::string& msg)
{
  if(msg.size() < MFSYNC_HEADER_SIZE)
  {
    spdlog::debug("get_requested_file_from_message on empty message");
    return std::nullopt;
  }

  std::string_view view{msg};
  view.remove_prefix(MFSYNC_HEADER_BEGIN.size());
  view.remove_suffix(view.size() - view.find(MFSYNC_HEADER_END));

  try
  {
    return nlohmann::json::parse(view);
  }
  catch(std::exception& er)
  {
    spdlog::debug("Json Error: {}", er.what());
  }

  return std::nullopt;
}

std::string wrap_with_header(const std::string& msg)
{
  std::stringstream message_sstring;
  message_sstring << MFSYNC_HEADER_BEGIN;
  message_sstring << msg;
  message_sstring << MFSYNC_HEADER_END;
  return message_sstring.str();
}

std::string create_file_message(const std::string& public_key, const std::string& msg)
{
  nlohmann::json j;
  j["type"] = "file";
  j["version"] = protocol::VERSION;
  j["public_key"] = public_key;
  j["message"] = msg;

  return wrap_with_header(j.dump());
}

std::string create_file_list_message(const std::string& public_key)
{
  nlohmann::json j;
  j["type"] = "file_list";
  j["version"] = protocol::VERSION;
  j["public_key"] = public_key;

  return wrap_with_header(j.dump());
}

std::string create_error_message(const std::string& reason)
{
  std::stringstream message_sstring;
  message_sstring << MFSYNC_HEADER_BEGIN;
  message_sstring << reason;
  message_sstring << MFSYNC_HEADER_BEGIN;
  return message_sstring.str();
}

std::string create_message_from_requested_file(const requested_file& file)
{
  nlohmann::json j = file;

  std::stringstream message_sstring;
  message_sstring << MFSYNC_HEADER_BEGIN;
  message_sstring << j.dump();
  message_sstring << MFSYNC_HEADER_END;
  return message_sstring.str();
}

std::optional<requested_file> get_requested_file_from_message(const std::string& message)
{
  if(message.size() < MFSYNC_HEADER_SIZE)
  {
    spdlog::debug("get_requested_file_from_message on empty message");
    return std::nullopt;
  }

  std::string_view view{message};
  view.remove_prefix(MFSYNC_HEADER_BEGIN.size());
  view.remove_suffix(view.size() - view.find(MFSYNC_HEADER_END));

  try
  {
    auto j = nlohmann::json::parse(view);
    return j.get<requested_file>();
  }
  catch(std::exception& er)
  {
    spdlog::debug("Json Error: {}", er.what());
    return std::nullopt;
  }
}

std::string create_host_announcement_message(const std::string& pub_key,
                                             unsigned short port)
{
  nlohmann::json j;
  j["public_key"] = pub_key;
  j["port"] = port;
  j["version"] = protocol::VERSION;

  std::stringstream message_sstring;
  message_sstring << MFSYNC_HEADER_BEGIN;
  message_sstring << j.dump();
  message_sstring << MFSYNC_HEADER_END;
  return message_sstring.str();
}

std::optional<host_information> get_host_info_from_message(const std::string& message,
                                            const boost::asio::ip::udp::endpoint& endpoint)
{
  if(message.size() < MFSYNC_HEADER_SIZE)
  {
    spdlog::debug("get_requested_file_from_message on empty message");
    return std::nullopt;
  }

  std::string_view view{message};
  view.remove_prefix(MFSYNC_HEADER_BEGIN.size());
  view.remove_suffix(view.size() - view.find(MFSYNC_HEADER_END));

  try
  {
    auto j = nlohmann::json::parse(view);
    auto result = j.get<host_information>();
    result.ip = boost::lexical_cast<std::string>(endpoint.address());
    return result;
  }
  catch(std::exception& er)
  {
    spdlog::debug("Json Error: {}", er.what());
    return std::nullopt;
  }
}

std::string create_message_from_file_info(const file_handler::stored_files& file_infos,
                                          unsigned short port)
{
  nlohmann::json json_array = nlohmann::json::array();
  nlohmann::json element;
  for(const auto& file_info : file_infos)
  {
    element = file_info;
    element["port"] = port;
    json_array.push_back(element);
    element.clear();
  }

  return json_array.dump();
}

std::vector<std::string> create_messages_from_file_info(const file_handler::stored_files& file_infos,
                                                        unsigned short port)
{
  std::string message_string;
  std::vector<std::string> result;

  nlohmann::json json_array = nlohmann::json::array();
  nlohmann::json element;
  for(const auto& file_info : file_infos)
  {
    element = file_info;
    element["port"] = port;
    nlohmann::json tmp_size_check = json_array;
    tmp_size_check.push_back(element);
    if(tmp_size_check.dump().size() > MAX_MESSAGE_SIZE)
    {
      result.push_back(json_array.dump());
      json_array.clear();
      json_array.push_back(element);
    }
    else
    {
      json_array = std::move(tmp_size_check);
    }

    element.clear();
  }

  if(!json_array.empty())
  {
    result.push_back(json_array.dump());
  }

  return result;
}

std::tuple<bool, std::string, crypto::encryption_wrapper> decompose_message(const std::string& message)
{
  const auto optional_json = protocol::get_json_from_message(message);
  if (!optional_json.has_value()) {
    return std::make_tuple(false, std::string{}, crypto::encryption_wrapper{});
  }

  try
  {
    const auto& wrapper_str =
        optional_json.value().at("message").get<std::string>();

    const auto wrapper = nlohmann::json::parse(wrapper_str).get<crypto::encryption_wrapper>();

    const auto public_key = optional_json.value().at("public_key").get<std::string>();

    return std::make_tuple(true, public_key, wrapper);
  }
  catch(nlohmann::json::parse_error& er)
  {
    spdlog::debug("Json Parse Error: {}", er.what());
    return std::make_tuple(false, std::string{}, crypto::encryption_wrapper{});
  }

}

std::optional<std::string> get_decrypted_message(const std::string& message, crypto::crypto_handler& handler)
{
  auto [no_error, public_key, wrapper] = decompose_message(message);

  if(!no_error)
  {
    return std::nullopt;
  }

  const auto decrypted =  handler.decrypt(public_key, wrapper);

  if(!decrypted.has_value())
  {
    return std::nullopt;
  }

  return std::string(reinterpret_cast<const char*>(decrypted.value().cipher_text.data()),
                     decrypted.value().cipher_text.size());
}

std::optional<std::string> get_decrypted_message(const std::string& message, const std::string& public_key, crypto::crypto_handler& handler)
{
  try
  {
    const auto optional_json = protocol::get_json_from_message(message);
    if(!optional_json.has_value())
    {
      return std::nullopt;
    }

    const auto wrapper = optional_json.value().get<crypto::encryption_wrapper>();
    const auto decrypted =  handler.decrypt(public_key, wrapper);

    if(!decrypted.has_value())
    {
      return std::nullopt;
    }

    return std::string(reinterpret_cast<const char*>(decrypted.value().cipher_text.data()),
                       decrypted.value().cipher_text.size());
  }
  catch(nlohmann::json::parse_error& er)
  {
    spdlog::debug("Json Parse Error: {}", er.what());
    return std::nullopt;
  }
}


std::optional<file_handler::available_files>
get_available_files_from_message(const std::string& message,
                                 const boost::asio::ip::tcp::endpoint& endpoint,
                                 const std::string& pub_key)
{
  return get_available_files_from_message(message, endpoint.address(), pub_key);
}

std::optional<file_handler::available_files>
get_available_files_from_message(const std::string& message,
                                 const boost::asio::ip::udp::endpoint& endpoint,
                                 const std::string& pub_key)
{
  return get_available_files_from_message(message, endpoint.address(), pub_key);
}

std::optional<size_t> get_count_from_message(const std::string& message)
{
  if(message.empty())
  {
    spdlog::debug("get_file_info_from_message on empty message");
    return std::nullopt;
  }

  try
  {
    const auto j = get_json_from_message(message);

    if(!j.has_value()) {
      return std::nullopt;
    }

    return j.value().at("count").get<size_t>();
  }
  catch(nlohmann::json::parse_error& er)
  {
    spdlog::debug("Json Parse Error: {}", er.what());
    return std::nullopt;
  }
}

std::optional<file_handler::available_files>
get_available_files_from_message(const std::string& message,
                                 const boost::asio::ip::address& address,
                                 const std::string& pub_key)
{
  if(message.empty())
  {
    spdlog::debug("get_file_info_from_message on empty message");
    return std::nullopt;
  }

  try
  {
    const auto j = nlohmann::json::parse(message);

    file_handler::available_files result;
    for(const auto& available : j)
    {
      available_file av = available.get<available_file>();
      av.source_address = address;
      av.public_key = pub_key;
      result.insert(av);
    }

    return result;
  }
  catch(nlohmann::json::parse_error& er)
  {
    spdlog::debug("Json Parse Error: {}", er.what());
    return std::nullopt;
  }
}

}
