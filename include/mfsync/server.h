#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "mfsync/file_handler.h"
#include "mfsync/server_session.h"

namespace mfsync::filetransfer
{

class server
{
public:
  server(boost::asio::io_context &io_context, unsigned short port, mfsync::file_handler& file_handler);

  void run();
  void stop();

private:

  void start_listening(uint16_t port);
  void accept_connections();
  void handle_new_connection(boost::asio::ip::tcp::socket socket, const boost::system::error_code &ec);

  std::string get_password() const;

  boost::asio::io_context &io_context_;
  boost::asio::ssl::context ssl_context_;

  boost::asio::ip::tcp::acceptor acceptor_;
  unsigned short port_;
  mfsync::file_handler& file_handler_;
};

} //closing namespace mfsync::filetransfer
