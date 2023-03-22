#pragma once

#include "spdlog/spdlog.h"

#include <boost/asio.hpp>
#include "boost/bind.hpp"
#include "boost/lexical_cast.hpp"

#include "mfsync/file_handler.h"
#include "mfsync/crypto.h"

namespace mfsync::multicast
{

class file_fetcher
{
public:
  file_fetcher(boost::asio::io_service& io_service,
               const boost::asio::ip::address& listen_address,
               const boost::asio::ip::address& multicast_address,
               const short multicast_port,
               mfsync::file_handler* file_handler,
               mfsync::crypto::crypto_handler& crypto_handler);

  void handle_receive_from(const boost::system::error_code& error, size_t bytes_recvd);
  void list_hosts(bool value) { list_host_infos_ = value; }

private:
  void print_host_if_new(const host_information& host_info)
  {
    if(host_infos_.contains(host_info.public_key))
    {
      return;
    }

    spdlog::info("{} - {}:{} - v{}", host_info.public_key,
                                     host_info.ip,
                                     host_info.port,
                                     host_info.version);

    host_infos_.emplace(host_info.public_key);
  }

  void do_receive()
  {
    socket_.async_receive_from(boost::asio::buffer(data_, max_length), sender_endpoint_,
                               boost::bind(&file_fetcher::handle_receive_from, this,
                                           boost::asio::placeholders::error,
                                           boost::asio::placeholders::bytes_transferred));
  }

  mutable std::mutex mutex_;
  boost::asio::io_context& io_context_;
  boost::asio::ip::udp::socket socket_;
  mfsync::file_handler* file_handler_;
  mfsync::crypto::crypto_handler& crypto_handler_;
  boost::asio::ip::udp::endpoint sender_endpoint_;
  bool list_host_infos_ = false;
  std::set<std::string> host_infos_;
  enum { max_length = 1024 };
  char data_[max_length];
};

} //closing namespace mfsync::multicast
