#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "mfsync/file_handler.h"
#include "mfsync/deque.h"

namespace mfsync::filetransfer
{

template<typename SocketType>
class client_session_base : public std::enable_shared_from_this<client_session_base<SocketType>>
{
public:
  client_session_base() = delete;
  client_session_base(boost::asio::io_context& context,
                      SocketType socket,
                      mfsync::concurrent::deque<available_file>& deque,
                      mfsync::file_handler& handler);

  SocketType& get_socket();
  virtual void start_request() = 0;
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
  std::string message_;
  boost::asio::streambuf stream_buffer_;
  std::vector<uint8_t> readbuf_;
  mfsync::ofstream_wrapper ofstream_;
};

class client_session : public client_session_base<boost::asio::ip::tcp::socket>
                       //public std::enable_shared_from_this<client_session>
{
public:
  client_session() = delete;
  client_session(boost::asio::io_context& context,
                 boost::asio::ip::tcp::socket socket,
                 mfsync::concurrent::deque<available_file>& deque,
                 mfsync::file_handler& handler);

  virtual void start_request() override;
};

class client_tls_session : public client_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
                           //public std::enable_shared_from_this<client_tls_session>
{
public:
  using base = client_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
  client_tls_session() = delete;
  client_tls_session(boost::asio::io_context& context,
                     boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
                     mfsync::concurrent::deque<available_file>& deque,
                     mfsync::file_handler& handler);

  virtual void start_request() override;
  bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx);
  void handshake();
};

} //closing namespace mfsync::filetransfer
