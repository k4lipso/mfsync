#pragma once

#include <deque>
#include <memory>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/file_handler.h"
#include "mfsync/protocol.h"
#include "mfsync/deque.h"
#include "mfsync/server_session.h"
#include "mfsync/client_session.h"

namespace mfsync
{


namespace filetransfer
{

class server
{
public:
  server(boost::asio::io_context &io_context, unsigned short port, mfsync::file_handler& file_handler)
    : io_context_(io_context)
    , acceptor_(io_context_)
    , port_(port)
    , file_handler_(file_handler)
  {}

  void run()
  {
    start_listening(port_);
    accept_connections();
  }

  void stop()
  {
    acceptor_.close();
    spdlog::debug("[Server] closed acceptor");
  }

private:

  void start_listening(uint16_t port)
  {
    spdlog::debug("setting up endpoint");
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
    spdlog::debug("setting port to {}", port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    spdlog::debug("started listening");
  }

  void accept_connections()
  {
    auto handler = std::make_shared<mfsync::filetransfer::server_session>(io_context_, file_handler_);
    acceptor_.async_accept(handler->get_socket(), [this, handler](auto ec) { handle_new_connection(handler, ec); });
  }

  void handle_new_connection(std::shared_ptr<mfsync::filetransfer::server_session> handler,
                             const boost::system::error_code &ec)
  {
    if(ec)
    {
      spdlog::error("handle_accept with error: {}", ec.message());
      return;
    }

    if(!acceptor_.is_open())
    {
      spdlog::error("acceptor was closed.");
      return;
    }

    handler->read();

    auto new_handler = std::make_shared<mfsync::filetransfer::server_session>(io_context_, file_handler_);

    acceptor_.async_accept(new_handler->get_socket(),
                           [this, new_handler](auto ec) { handle_new_connection(new_handler, ec); });
  }

  boost::asio::io_context &io_context_;
  boost::asio::ip::tcp::acceptor acceptor_;
  unsigned short port_;
  mfsync::file_handler& file_handler_;
};

}

class file_receive_handler
{
public:
  file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler);

  file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler,
                       std::vector<std::string> files_to_request);

  void set_files(std::vector<std::string> files_to_request);
  void request_all_files();
  void get_files();

private:

  void try_start_new_session();
  void request_file(available_file file);
  void wait_for_new_files();
  void handle_timeout(const boost::system::error_code& error);

  boost::asio::io_context& io_context_;
  boost::asio::deadline_timer timer_;
  mfsync::file_handler& file_handler_;
  std::vector<std::string> files_to_request_;
  mfsync::concurrent::deque<available_file> request_queue_;
  bool request_all_;
  std::weak_ptr<mfsync::filetransfer::client_session> session_;
  mutable std::mutex mutex_;
};

} //closing namespace mfsync
