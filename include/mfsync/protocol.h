#pragma once

#include <vector>
#include <optional>
#include <string>

#include "mfsync/file_handler.h"

namespace mfsync::protocol
{
  constexpr auto TCP_PORT = 8000;
  constexpr auto MAX_MESSAGE_SIZE = 512;

  std::vector<std::string> create_messages_from_file_info(const file_handler::stored_files& file_infos);

  std::optional<file_handler::available_files>
  get_available_files_from_message(const std::string& message,
                                   const boost::asio::ip::udp::endpoint& endpoint);
}

