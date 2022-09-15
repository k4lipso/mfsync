#include "mfsync/server.h"

#include <boost/bind.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/protocol.h"

namespace mfsync::filetransfer
{
server::server(boost::asio::io_context &io_context, unsigned short port, mfsync::file_handler& file_handler)
  : acceptor_(io_context)
  , port_(port)
  , file_handler_(file_handler)
{}

void server::run()
{
  if(start_listening(port_))
  {
    accept_connections();
  }
}

std::string server::get_password() const
{
  //todo: fix
  return "test";
}

void server::stop()
{
  acceptor_.close();
  spdlog::debug("closed acceptor");
}

void server::enable_tls(const std::string& dh_file, const std::string& cert_file, const std::string& key_file)
{

  ssl_context_ = boost::asio::ssl::context{boost::asio::ssl::context::sslv23};

  auto& ctx = ssl_context_.value();
  ctx.set_options(
    boost::asio::ssl::context::default_workarounds
    | boost::asio::ssl::context::no_sslv2
    | boost::asio::ssl::context::single_dh_use);

  ctx.set_password_callback(std::bind(&server::get_password, this));
  ctx.use_certificate_chain_file(cert_file);
  ctx.use_private_key_file(key_file.empty() ? cert_file : key_file, boost::asio::ssl::context::pem);
  ctx.use_tmp_dh_file(dh_file);
}

bool server::start_listening(uint16_t port)
{
  spdlog::debug("setting up endpoint");
  boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
  spdlog::debug("setting port to {}", port);
  acceptor_.open(endpoint.protocol());
  acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
  try
  {
    acceptor_.bind(endpoint);
    acceptor_.listen();
  }
  catch (std::exception& e)
  {
    spdlog::info("Port {} already in use, mfsync will not be able to send files. Use '--port' to specify a different port", port);
    return false;
  }

  spdlog::debug("started listening");
  return true;
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

  if(ssl_context_.has_value())
  {
    auto handler = std::make_shared<mfsync::filetransfer::server_tls_session>(
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(std::move(socket), ssl_context_.value()), file_handler_);
    handler->set_progress(progress_);
    handler->start();
  }
  else
  {
    auto handler = std::make_shared<mfsync::filetransfer::server_session>(std::move(socket), file_handler_);
    handler->set_progress(progress_);
    handler->start();
  }

  acceptor_.async_accept([this](auto ec, auto socket) { handle_new_connection(std::move(socket), ec); });
}

} //closing namespace mfsync::filetransfer
