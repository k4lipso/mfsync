#include <boost/asio.hpp>

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
  void handle_new_connection(std::shared_ptr<mfsync::filetransfer::server_session> handler,
                             const boost::system::error_code &ec);

  boost::asio::io_context &io_context_;
  boost::asio::ip::tcp::acceptor acceptor_;
  unsigned short port_;
  mfsync::file_handler& file_handler_;
};

} //closing namespace mfsync::filetransfer
