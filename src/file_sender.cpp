#include "mfsync/file_sender.h"

namespace mfsync::multicast
{
  file_sender::file_sender(boost::asio::io_service& io_service,
                           const boost::asio::ip::address& multicast_address,
                           short multicast_port,
                           unsigned short tcp_port,
                           file_handler& filehandler)
    : endpoint_(multicast_address, multicast_port)
    , socket_(io_service, endpoint_.protocol())
    , timer_(io_service)
    , port_(tcp_port)
    , file_handler_(filehandler)
  {}

  void file_sender::init()
  {
    auto messages = protocol::create_messages_from_file_info(file_handler_.get_stored_files(), port_);

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

  void file_sender::set_outbound_interface(const boost::asio::ip::address_v4& address)
  {
    boost::asio::ip::multicast::outbound_interface option(address);
    socket_.set_option(option);
  }

  void file_sender::handle_send_to(const boost::system::error_code& error)
  {
    if(!error)
    {
      timer_.expires_from_now(boost::posix_time::seconds(1));
      timer_.async_wait( std::bind(&file_sender::handle_timeout, this,
            std::placeholders::_1));
    }
  }

  void file_sender::handle_timeout(const boost::system::error_code& error)
  {
    if (!error)
    {
      auto messages = protocol::create_messages_from_file_info(file_handler_.get_stored_files(), port_);

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
} //closing namespace mfsync::multicast
