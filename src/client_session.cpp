#include "mfsync/client_session.h"

#include <boost/bind.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/protocol.h"

namespace mfsync::filetransfer
{

client_session::client_session(boost::asio::io_context& context,
               mfsync::concurrent::deque<available_file>& deque,
               mfsync::file_handler& handler)
  : io_context_(context)
  , socket_(context)
  , deque_(deque)
  , file_handler_(handler)
{}

boost::asio::ip::tcp::socket& client_session::get_socket()
{
  return socket_;
}

void client_session::start_request()
{
  auto available = deque_.try_pop();

  if(!available.has_value())
  {
    return;
  }

  //not setting offset here, it will be set by file_handler when file is created
  requested_.file_info = std::move(available.value().file_info);
  requested_.chunksize = mfsync::protocol::CHUNKSIZE;

  boost::asio::ip::tcp::resolver resolver{io_context_};
  auto endpoint = resolver.resolve(available.value().source_address.to_string(),
                                   std::to_string(available.value().source_port));

  boost::asio::async_connect(
    socket_,
    endpoint,
    [this, me = shared_from_this(), available]
    (boost::system::error_code ec, boost::asio::ip::tcp::endpoint)
    {
      if(!ec)
      {
        me->request_file();
      }
      else
      {
        spdlog::debug("Couldnt conntect. error: {}", ec.message());
        spdlog::debug("Target host: {} {}", available.value().source_address.to_string(),
                                            available.value().source_port);
      }
    });
}

void client_session::request_file()
{
  auto output_file_stream = file_handler_.create_file(requested_);

  if(!output_file_stream.has_value())
  {
    spdlog::debug("file creation failed. abort session");
    handle_error();
    return;
  }

  ofstream_ = std::move(output_file_stream.value());

  message_ = protocol::create_message_from_requested_file(requested_);

  spdlog::debug("Sending message: {}", message_);

  async_write(socket_,
    boost::asio::buffer(message_.data(), message_.size()),
    [me = shared_from_this()](boost::system::error_code const &ec, std::size_t) {
      if(!ec)
      {
        spdlog::debug("Done requesting file!");
        me->read_file_request_response();
      }
      else
      {
        spdlog::debug("async write failed: {}", ec.message());
      }
    });
}

void client_session::read_file_request_response()
{
  boost::asio::async_read_until(
    socket_,
    stream_buffer_,
    mfsync::protocol::MFSYNC_HEADER_END,
    [me = shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
    {
      me->handle_read_file_request_response(error, bytes_transferred);
    });
}

void client_session::handle_read_file_request_response(boost::system::error_code const &error,
                                                       std::size_t bytes_transferred)
{
  if(!error)
  {
    boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
    std::string response_message(
      boost::asio::buffers_begin(bufs),
      boost::asio::buffers_begin(bufs) + bytes_transferred);

    stream_buffer_.consume(bytes_transferred);

    spdlog::debug("Received file_request_response: {}", response_message);

    if(response_message != protocol::create_begin_transmission_message())
    {
      spdlog::debug("Server returned with error: {}", response_message);
      handle_error();
      return;
    }

    message_ = std::move(response_message);
    spdlog::debug("Sending response: {}", message_);

    bytes_written_to_requested_ = requested_.offset;

    async_write(socket_,
      boost::asio::buffer(message_.data(), message_.size()),
      [me = shared_from_this()](boost::system::error_code const &ec, std::size_t) {
        if(!ec)
        {
          spdlog::debug("Done sending response");
          me->read_file_chunk();
        }
        else
        {
          spdlog::debug("async write failed: {}", ec.message());
          me->handle_error();
        }
      });
  }
  else
  {
    spdlog::debug("Error on handle_read_file_request_response: {}", error.message());
  }
}

void client_session::read_file_chunk()
{
  readbuf_.resize(requested_.chunksize);

  auto bytes_left = requested_.file_info.size - bytes_written_to_requested_;

  if(bytes_left <= 0)
  {
    spdlog::debug("complete file is written!");
    return;
  }

  if(bytes_left > requested_.chunksize)
  {
    bytes_left = requested_.chunksize;
  }

  boost::asio::mutable_buffers_1 buf = boost::asio::buffer(&readbuf_[0], bytes_left);
  boost::asio::async_read(
    socket_,
    buf,
    [me = shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
    {
      me->handle_read_file_chunk(error, bytes_transferred);
    });
}


void client_session::handle_read_file_chunk(boost::system::error_code const &error,
                                            std::size_t bytes_transferred)
{
  if(error)
  {
    spdlog::debug("error during read_file_chunk: {}", error.message());
    handle_error();
    return;
  }

  ofstream_.write(reinterpret_cast<char*>(readbuf_.data()), bytes_transferred, bytes_written_to_requested_);
  bytes_written_to_requested_ += bytes_transferred;

  spdlog::debug("received. bytes written: {} of: {}", ofstream_.tellp(), requested_.file_info.size);
  if(ofstream_.tellp() < static_cast<std::streamsize>(requested_.file_info.size))
  {
    read_file_chunk();
    return;
  }

  spdlog::debug("received file {}", requested_.file_info.file_name);
  spdlog::debug("with size in mb: {}", static_cast<double>(requested_.file_info.size / 1048576.0));

  ofstream_.flush();

  if(!file_handler_.finalize_file(requested_.file_info))
  {
    spdlog::debug("finalizing failed!!!");
    handle_error();
    return;
  }

  spdlog::info("received file: {} - {} - {}", requested_.file_info.file_name,
                                              requested_.file_info.sha256sum,
                                              requested_.file_info.size);

  readbuf_.clear();
  start_request();
}

void client_session::handle_error()
{
  spdlog::error("handle_error not implemented yet!!!");
}

} //closing namespace mfsync::filetransfer
