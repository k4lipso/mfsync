#include "mfsync/protocol.h"

#include <functional>
#include <sstream>

#include "spdlog/spdlog.h"

namespace mfsync::protocol
{

std::vector<std::string> create_messages_from_file_info(const file_handler::stored_files& file_infos)
{
  std::string message_string;
  std::vector<std::string> result;

  for(const auto& file_info : file_infos)
  {
    std::stringstream message_sstring;
    message_sstring << file_info.file_name << "?";
    message_sstring << file_info.sha256sum << "?";
    message_sstring << std::to_string(file_info.size) << "?";
    message_sstring << std::to_string(TCP_PORT) << "?";

    message_sstring.seekg(0, std::ios::end);
    auto message_sstring_size = message_sstring.tellg();

    if(message_string.size() + message_sstring_size > MAX_MESSAGE_SIZE)
    {
      result.push_back(message_string);
      message_string.clear();
    }

    message_string += message_sstring.str();
  }

  if(!message_string.empty())
  {
    result.push_back(message_string);
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

  std::string tmp;
  std::stringstream message_sstring{message};
  std::vector<std::string> tokens;

  while(std::getline(message_sstring, tmp, '?'))
  {
    tokens.push_back(tmp);
  }

  if(tokens.size() % 4 != 0)
  {
    spdlog::error("could not generate file_information out of message");
    return std::nullopt;
  }

  file_handler::available_files result;

  for(unsigned i = 0; i < tokens.size(); i += 4)
  {
    available_file available;
    available.file_info.file_name = tokens.at(i);
    available.file_info.sha256sum = tokens.at(i + 1);
    available.file_info.size = std::stoull(tokens.at(i + 2));
    available.source_address = endpoint.address();
    available.source_port = std::stoi(tokens.at(i + 3));
    result.insert(available);
  }

  return result;
}

}
