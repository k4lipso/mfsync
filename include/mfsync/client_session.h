#pragma once

#include <memory>

#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/file_handler.h"
#include "mfsync/deque.h"
#include "mfsync/progress_handler.h"
#include "mfsync/crypto.h"

namespace mfsync::filetransfer
{

class session_base
{
public:
  virtual ~session_base() = default;

  virtual void start_request() = 0;

  void set_progress(progress_handler* progress)
  {
    progress_ = progress;
  }

protected:
  progress_handler* progress_ = nullptr;
};

template<typename SocketType>
class client_encrypted_session
    : public session_base,
      public std::enable_shared_from_this<client_encrypted_session<SocketType>>
{
public:
  client_encrypted_session() = delete;
  client_encrypted_session(boost::asio::io_context& context,
                           SocketType socket,
                           mfsync::file_handler& handler,
                           mfsync::crypto::crypto_handler& crypto_handler,
                           mfsync::host_information host_info);
  virtual ~client_encrypted_session() = default;

  void initialize_communication();
  void read_encrypted_response();
  void handle_read_encrypted_response(boost::system::error_code const &error, std::size_t bytes_transferred);
  void request_file_list();

protected:
  boost::asio::io_context& io_context_;
  SocketType socket_;
  mfsync::file_handler& file_handler_;
  mfsync::crypto::crypto_handler& crypto_handler_;
  mfsync::host_information host_info_;
  std::string message_;
  boost::asio::streambuf stream_buffer_;
  std::vector<uint8_t> readbuf_;
};

class client_encrypted_file_list : public client_encrypted_session<boost::asio::ip::tcp::socket>
{
public:
  client_encrypted_file_list() = delete;
  client_encrypted_file_list(boost::asio::io_context& context,
                             mfsync::file_handler& handler,
                             mfsync::crypto::crypto_handler& crypto_handler,
                             mfsync::host_information host_info);
  virtual ~client_encrypted_file_list() = default;

  virtual void start_request() override;
};

template<typename SocketType>
class client_session_base : public session_base, public std::enable_shared_from_this<client_session_base<SocketType>>
{
public:
  client_session_base() = delete;
  client_session_base(boost::asio::io_context& context,
                      SocketType socket,
                      mfsync::concurrent::deque<available_file>& deque,
                      mfsync::file_handler& handler,
                      mfsync::crypto::crypto_handler& crypto_handler);
  virtual ~client_session_base() = default;

  SocketType& get_socket();
  void request_file();

protected:
  void read_file_request_response();
  void handle_read_file_request_response(boost::system::error_code const &error, std::size_t bytes_transferred);
  void read_file_chunk();
  void handle_read_file_chunk(boost::system::error_code const &error, std::size_t bytes_transferred);
  void handle_error();

  boost::asio::io_context& io_context_;
  SocketType socket_;
  requested_file requested_;
  size_t bytes_written_to_requested_ = 0;
  mfsync::concurrent::deque<available_file>& deque_;
  mfsync::file_handler& file_handler_;
  mfsync::crypto::crypto_handler& crypto_handler_;
  std::string pub_key_;
  std::string message_;
  boost::asio::streambuf stream_buffer_;
  std::vector<uint8_t> readbuf_;
  mfsync::ofstream_wrapper ofstream_;
  progress::file_progress_information* bar_ = nullptr;
};

class client_session : public client_session_base<boost::asio::ip::tcp::socket>
{
public:
  client_session() = delete;
  client_session(boost::asio::io_context& context,
                 mfsync::concurrent::deque<available_file>& deque,
                 mfsync::file_handler& handler,
                 mfsync::crypto::crypto_handler& crypto_handler);
  virtual ~client_session() = default;

  virtual void start_request() override;
};

class client_tls_session : public client_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
{
public:
  using base = client_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
  client_tls_session() = delete;
  client_tls_session(boost::asio::io_context& context,
                     boost::asio::ssl::context& ssl_context,
                     mfsync::concurrent::deque<available_file>& deque,
                     mfsync::file_handler& handler,
                     mfsync::crypto::crypto_handler& crypto_handler);
  virtual ~client_tls_session() = default;

  virtual void start_request() override;
  bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx);
  void handshake();
};

} //closing namespace mfsync::filetransfer
