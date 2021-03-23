#include <boost/asio.hpp>

#include "mfsync/file_handler.h"

namespace mfsync::filetransfer
{

class server_session : public std::enable_shared_from_this<server_session>
{
public:
  server_session(boost::asio::io_context& context,
                 mfsync::file_handler& handler);

  boost::asio::ip::tcp::socket& get_socket();
  void read();

private:

  void handle_read_header(boost::system::error_code const &error, std::size_t bytes_transferred);
  void send_confirmation();
  void reply_with_error(const std::string& reason);
  void read_confirmation();
  void handle_read_confirmation(boost::system::error_code const &error, std::size_t bytes_transferred);
  void write_file();

  boost::asio::io_context& io_context_;
  boost::asio::ip::tcp::socket socket_;
  mfsync::file_handler& file_handler_;
  std::string message_;
  requested_file requested_;
  boost::asio::streambuf stream_buffer_;
  std::vector<char> writebuf_;
  std::ifstream ifstream_;
};

} //closing namespace mfsync::filetransfer
