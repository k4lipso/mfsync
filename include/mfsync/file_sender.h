#pragma once

#include "spdlog/spdlog.h"
#include <boost/asio.hpp>
#include "boost/bind.hpp"
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "mfsync/file_handler.h"

namespace mfsync::protocol
{
  constexpr auto tcp_port = 8000;

  inline std::vector<std::string> create_messages_from_file_info(const file_handler::stored_files& file_infos)
  {
    std::string message_string;
    std::vector<std::string> result;

    for(const auto& file_info : file_infos)
    {
      std::stringstream message_sstring;
      message_sstring << file_info.file_name << "?";
      message_sstring << file_info.sha256sum << "?";
      message_sstring << std::to_string(file_info.size) << "?";
      message_sstring << std::to_string(tcp_port) << "?";

      message_sstring.seekg(0, std::ios::end);
      auto message_sstring_size = message_sstring.tellg();

      if(message_string.size() + message_sstring_size > 512)
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

  inline std::optional<file_handler::available_files>
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


namespace mfsync::multicast
{

  class file_sender
  {
  public:
    file_sender(boost::asio::io_service& io_service,
        const boost::asio::ip::address& multicast_address,
        short multicast_port,
        file_handler* filehandler)
      : endpoint_(multicast_address, multicast_port)
      , socket_(io_service, endpoint_.protocol())
      , timer_(io_service)
      , message_count_(0)
      , file_handler_(filehandler)

    {
      init();
    }

    void init()
    {
      auto messages = protocol::create_messages_from_file_info(file_handler_->get_stored_files());

      if(messages.empty())
      {
        timer_.expires_from_now(boost::posix_time::seconds(1));
        timer_.async_wait(std::bind(&file_sender::init, this));
        return;
      }

      for(const auto& message : messages)
      {
        socket_.async_send_to(
            boost::asio::buffer(message), endpoint_,
            std::bind(&file_sender::handle_send_to, this,
              std::placeholders::_1));
      }

    }

    void handle_send_to(const boost::system::error_code& error)
    {
      if(!error && message_count_ < 100)
      {
        timer_.expires_from_now(boost::posix_time::seconds(1));
        timer_.async_wait( std::bind(&file_sender::handle_timeout, this,
              std::placeholders::_1));
      }
    }

    void handle_timeout(const boost::system::error_code& error)
    {
      if (!error)
      {
        auto messages = protocol::create_messages_from_file_info(file_handler_->get_stored_files());

        if(!messages.empty())
        {
          message_ = messages.front();
        }

        for(const auto& message : messages)
        {
          if(message.empty())
          {
            continue;
          }

          spdlog::debug("Sending Message: '{}'", message);
          socket_.async_send_to(
              boost::asio::buffer(message), endpoint_,
              std::bind(&file_sender::handle_send_to, this,
                std::placeholders::_1));
        }
      }
    }

  private:
    boost::asio::ip::udp::endpoint endpoint_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::deadline_timer timer_;
    int message_count_;
    std::string message_;

    file_handler* file_handler_;
  };
} //closing namespace mfsync::multicast
