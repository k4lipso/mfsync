#pragma once

#include "spdlog/spdlog.h"
#include <boost/asio.hpp>
#include "boost/bind.hpp"
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "mfsync/file_handler.h"
#include "mfsync/protocol.h"

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
