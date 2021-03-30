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
                unsigned short tcp_port,
                file_handler& filehandler);

    void init();
    void set_outbound_interface(const boost::asio::ip::address_v4& address);
    void handle_send_to(const boost::system::error_code& error);
    void handle_timeout(const boost::system::error_code& error);

  private:
    boost::asio::ip::udp::endpoint endpoint_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::deadline_timer timer_;
    std::string message_;
    unsigned short port_;
    file_handler& file_handler_;
  };
} //closing namespace mfsync::multicast
