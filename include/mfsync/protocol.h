#pragma once

#include <vector>
#include <optional>
#include <string>
#include <string_view>

#include "mfsync/file_handler.h"

namespace mfsync::protocol
{
  constexpr auto TCP_PORT = 8000;
  constexpr auto MULTICAST_PORT = 30001;
  constexpr auto MULTICAST_LISTEN_ADDRESS = "0.0.0.0";
  constexpr auto MULTICAST_ADDRESS = "239.255.0.1";
  constexpr auto MAX_MESSAGE_SIZE = 1024;
  constexpr auto CHUNKSIZE = 1024;
  constexpr std::string_view MFSYNC_HEADER_BEGIN = "<MFSYNC_HEADER_BEGIN>";
  constexpr std::string_view MFSYNC_HEADER_END = "<MFSYNC_HEADER_END>";
  constexpr auto MFSYNC_HEADER_SIZE = MFSYNC_HEADER_BEGIN.size() + MFSYNC_HEADER_END.size();
  constexpr auto MFSYNC_LOG_PREFIX = "";
  constexpr auto VERSION = "0.1.0";

  constexpr std::string_view create_begin_transmission_message()
  {
    return "<MFSYNC_HEADER_BEGIN>BEGIN_TRANSMISSION<MFSYNC_HEADER_END>";
  }

  std::string create_error_message(const std::string& reason);

  std::string create_message_from_requested_file(const requested_file& file);
  std::optional<requested_file> get_requested_file_from_message(const std::string& message);

  std::vector<std::string> create_messages_from_file_info(const file_handler::stored_files& file_infos,
                                                          unsigned short port);

  std::optional<file_handler::available_files>
  get_available_files_from_message(const std::string& message,
                                   const boost::asio::ip::udp::endpoint& endpoint);
}

