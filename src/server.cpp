#include "mfsync/server.h"

#include <boost/bind.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/protocol.h"

namespace mfsync::filetransfer
{
server::server(boost::asio::io_context &io_context, unsigned short port, mfsync::file_handler& file_handler)
  : io_context_(io_context)
  , ssl_context_(boost::asio::ssl::context::sslv23)
  , acceptor_(io_context_)
  , port_(port)
  , file_handler_(file_handler)
{}

void server::run()
{
  ssl_context_.set_options(
    boost::asio::ssl::context::default_workarounds
    | boost::asio::ssl::context::no_sslv2
    | boost::asio::ssl::context::single_dh_use);
  ssl_context_.set_password_callback(std::bind(&server::get_password, this));
  ssl_context_.use_certificate_chain_file("server.pem");
  ssl_context_.use_private_key_file("server.pem", boost::asio::ssl::context::pem);
  ssl_context_.use_tmp_dh_file("dh2048.pem");

  start_listening(port_);
  accept_connections();
}

std::string server::get_password() const
{
  return "test";
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
  acceptor_.async_accept([this](auto ec, auto socket) { handle_new_connection(std::move(socket), ec); });
}

void server::handle_new_connection(boost::asio::ip::tcp::socket socket,
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

  auto handler = std::make_shared<mfsync::filetransfer::server_tls_session>(
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(std::move(socket), ssl_context_), file_handler_);

  handler->start();

  acceptor_.async_accept([this](auto ec, auto socket) { handle_new_connection(std::move(socket), ec); });
}

} //closing namespace mfsync::filetransfer
