#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "mfsync/file_handler.h"

namespace mfsync::filetransfer
{

using SSLSocket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;

template<typename SocketType>
class server_session_base : public std::enable_shared_from_this<server_session_base<SocketType>>
{
public:
  server_session_base() = delete;
  server_session_base(SocketType socket,
                      mfsync::file_handler& handler);
  virtual ~server_session_base() = default;

  SocketType& get_socket();
  virtual void start() = 0;
  void read();

protected:

  void handle_read_header(boost::system::error_code const &error, std::size_t bytes_transferred);
  void send_confirmation();
  void reply_with_error(const std::string& reason);
  void read_confirmation();
  void handle_read_confirmation(boost::system::error_code const &error, std::size_t bytes_transferred);
  void write_file();

  SocketType socket_;
  mfsync::file_handler& file_handler_;
  std::string message_;
  requested_file requested_;
  boost::asio::streambuf stream_buffer_;
  std::vector<char> writebuf_;
  std::ifstream ifstream_;
};

class server_session : public server_session_base<boost::asio::ip::tcp::socket>
{
public:
  server_session() = delete;
  server_session(boost::asio::ip::tcp::socket socket, mfsync::file_handler& handler);
  virtual ~server_session() = default;

  virtual void start() override;
};

class server_tls_session : public server_session_base<SSLSocket>
{
public:
  server_tls_session() = delete;
  server_tls_session(SSLSocket socket, mfsync::file_handler& handler);
  virtual ~server_tls_session() = default;

  virtual void start() override;

private:
  void do_handshake();
};

} //closing namespace mfsync::filetransfer
