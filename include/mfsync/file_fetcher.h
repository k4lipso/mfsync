#pragma once

#include <boost/asio.hpp>

#include "boost/bind.hpp"
#include "boost/lexical_cast.hpp"
#include "mfsync/crypto.h"
#include "mfsync/file_handler.h"
#include "spdlog/spdlog.h"

namespace mfsync::multicast {

class file_fetcher {
 public:
  file_fetcher(boost::asio::io_service& io_service,
               const boost::asio::ip::address& listen_address,
               const boost::asio::ip::address& multicast_address,
               const short multicast_port, mfsync::file_handler* file_handler,
               mfsync::crypto::crypto_handler& crypto_handler);

  void handle_receive_from(const boost::system::error_code& error,
                           size_t bytes_recvd);
  void list_hosts(bool value) { list_host_infos_ = value; }

 private:
  void print_host(const host_information& host_info);

  void do_receive() {
    socket_.async_receive_from(
        boost::asio::buffer(data_, max_length), sender_endpoint_,
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

}  // namespace mfsync::multicast
