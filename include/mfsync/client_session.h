#include <memory>

#include <boost/asio.hpp>

#include "mfsync/file_handler.h"
#include "mfsync/deque.h"

namespace mfsync::filetransfer
{

class client_session : public std::enable_shared_from_this<client_session>
{
public:
  client_session() = delete;
  client_session(boost::asio::io_context& context,
                 mfsync::concurrent::deque<available_file>& deque,
                 mfsync::file_handler& handler);

  boost::asio::ip::tcp::socket& get_socket();
  void start_request();

private:

  void request_file();
  void read_file_request_response();
  void handle_read_file_request_response(boost::system::error_code const &error, std::size_t bytes_transferred);
  void read_file_chunk();
  void handle_read_file_chunk(boost::system::error_code const &error, std::size_t bytes_transferred);
  void handle_error();

  boost::asio::io_context& io_context_;
  boost::asio::ip::tcp::socket socket_;
  requested_file requested_;
  size_t bytes_written_to_requested_ = 0;
  mfsync::concurrent::deque<available_file>& deque_;
  mfsync::file_handler& file_handler_;
  std::string message_;
  boost::asio::streambuf stream_buffer_;
  std::vector<uint8_t> readbuf_;
  mfsync::ofstream_wrapper ofstream_;
};


} //closing namespace mfsync::filetransfer
