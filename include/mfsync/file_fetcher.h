#pragma once

#include "spdlog/spdlog.h"

#include <boost/asio.hpp>
#include "boost/bind.hpp"
#include "boost/lexical_cast.hpp"

#include "mfsync/file_handler.h"

namespace mfsync::multicast
{

class file_fetcher
{
public:
  file_fetcher(boost::asio::io_service& io_service,
               const boost::asio::ip::address& listen_address,
               const boost::asio::ip::address& multicast_address,
               const short multicast_port,
               mfsync::file_handler* file_handler);

  void handle_receive_from(const boost::system::error_code& error, size_t bytes_recvd);

private:
  mutable std::mutex mutex_;
  boost::asio::ip::udp::socket socket_;
  mfsync::file_handler* file_handler_;
  boost::asio::ip::udp::endpoint sender_endpoint_;
  enum { max_length = 1024 };
  char data_[max_length];
};

} //closing namespace mfsync::multicast
