#include "mfsync/protocol.h"

#include <functional>
#include <sstream>
#include <nlohmann/json.hpp>

#include "spdlog/spdlog.h"

namespace mfsync::protocol
{

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

  nlohmann::json j;
  try
  {
    j = nlohmann::json::parse(view);
  }
  catch(nlohmann::json::parse_error& er)
  {
    spdlog::debug("Json Parse Error: {}", er.what());
    return std::nullopt;
  }

  return j.get<requested_file>();
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

std::optional<file_handler::available_files>
get_available_files_from_message(const std::string& message,
                                 const boost::asio::ip::udp::endpoint& endpoint)
{
  if(message.empty())
  {
    spdlog::debug("get_file_info_from_message on empty message");
    return std::nullopt;
  }

  nlohmann::json j;
  try
  {
    j = nlohmann::json::parse(message);
  }
  catch(nlohmann::json::parse_error& er)
  {
    spdlog::debug("Json Parse Error: {}", er.what());
    return std::nullopt;
  }

  file_handler::available_files result;
  for(const auto& available : j)
  {
    available_file av = available.get<available_file>();
    av.source_address = endpoint.address();
    result.insert(available);
  }

  return result;
  //std::string tmp;
  //std::stringstream message_sstring{message};
  //std::vector<std::string> tokens;

  //while(std::getline(message_sstring, tmp, '^'))
  //{
  //  tokens.push_back(tmp);
  //}

  //if(tokens.size() % 4 != 0)
  //{
  //  spdlog::error("could not generate file_information out of message");
  //  return std::nullopt;
  //}

  //file_handler::available_files result;

  //for(unsigned i = 0; i < tokens.size(); i += 4)
  //{
  //  available_file available;
  //  available.file_info.file_name = tokens.at(i);
  //  available.file_info.sha256sum = tokens.at(i + 1);
  //  available.file_info.size = std::stoull(tokens.at(i + 2));
  //  available.source_address = endpoint.address();
  //  available.source_port = std::stoi(tokens.at(i + 3));
  //  result.insert(available);
  //}

  //return result;
}

}
