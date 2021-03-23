#include "mfsync/server.h"

#include <boost/bind.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/protocol.h"

namespace mfsync::filetransfer
{
server::server(boost::asio::io_context &io_context, unsigned short port, mfsync::file_handler& file_handler)
  : io_context_(io_context)
  , acceptor_(io_context_)
  , port_(port)
  , file_handler_(file_handler)
{}

void server::run()
{
  start_listening(port_);
  accept_connections();
}

void server::stop()
{
  acceptor_.close();
  spdlog::debug("closed acceptor");
}

void server::start_listening(uint16_t port)
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

void server::accept_connections()
{
  auto handler = std::make_shared<mfsync::filetransfer::server_session>(io_context_, file_handler_);
  acceptor_.async_accept(handler->get_socket(),
                         [this, handler](auto ec) { handle_new_connection(handler, ec); });
}

void server::handle_new_connection(std::shared_ptr<mfsync::filetransfer::server_session> handler,
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

} //closing namespace mfsync::filetransfer
