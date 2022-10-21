#include "mfsync/server_session.h"

#include <boost/bind.hpp>

#include "spdlog/spdlog.h"
#include "mfsync/protocol.h"

namespace mfsync::filetransfer
{

template<typename SocketType>
server_session_base<SocketType>::server_session_base(SocketType socket,
                                                     mfsync::file_handler& handler)
  : socket_(std::move(socket))
  , file_handler_(handler)
{}

template<typename SocketType>
SocketType& server_session_base<SocketType>::get_socket()
{
  return socket_;
}

server_session::server_session(boost::asio::ip::tcp::socket socket, mfsync::file_handler& handler)
  : server_session_base<boost::asio::ip::tcp::socket>(std::move(socket), handler)
{}

server_tls_session::server_tls_session(boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket, mfsync::file_handler& handler)
  : server_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(std::move(socket), handler)
{}

void server_session::start()
{
  read();
}


void server_tls_session::start()
{
  do_handshake();
}

void server_tls_session::do_handshake()
{
  socket_.async_handshake(boost::asio::ssl::stream_base::server,
                          [this, me = shared_from_this()](const boost::system::error_code& error)
                          {
                            if(!error)
                            {
                              me->read();
                            }
                            else
                            {
                              spdlog::error("server_tls_session error on handshake: {}", error.message());
                            }
                          });
}

template<typename SocketType>
void server_session_base<SocketType>::read()
{
  boost::asio::async_read_until(
    socket_,
    stream_buffer_,
    mfsync::protocol::MFSYNC_HEADER_END,
    [me = this->shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
    {
      me->handle_read_header(error, bytes_transferred);
    });
}

template<typename SocketType>
void server_session_base<SocketType>::handle_read_header(boost::system::error_code const &error, std::size_t bytes_transferred)
{
  if(error)
  {
    spdlog::debug("Error on handle_read_header: {}", error.message());
    return;
  }

  stream_buffer_.commit(bytes_transferred);
  std::istream is(&stream_buffer_);
  std::string message(std::istreambuf_iterator<char>(is), {});
  spdlog::debug("Received header: {}", message);

  const auto file = protocol::get_requested_file_from_message(message);
  if(!file.has_value())
  {
    spdlog::debug("Couldnt create requested_file from message: {}", message);
    return;
  }

  if(file_handler_.is_stored(file.value().file_info))
  {
    requested_ = file.value();
    send_confirmation();
  }
  else
  {
    reply_with_error("file doesnt exists");
  }
}

template<typename SocketType>
void server_session_base<SocketType>::send_confirmation()
{
  message_ = protocol::create_begin_transmission_message();
  spdlog::debug("Sending response: {}", message_);
  async_write(socket_,
    boost::asio::buffer(message_.data(), message_.size()),
    [me = this->shared_from_this()](boost::system::error_code const &ec, std::size_t) {
      if(!ec)
      {
        spdlog::debug("Done sending response");
        me->read_confirmation();
      }
      else
      {
        spdlog::debug("async write failed: {}", ec.message());
      }
    });
}

template<typename SocketType>
void server_session_base<SocketType>::reply_with_error(const std::string& reason)
{
  message_ = protocol::create_error_message(reason);
  spdlog::debug("Sending response: {}", message_);
  async_write(socket_,
    boost::asio::buffer(message_.data(), message_.size()),
    [me = this->shared_from_this()](boost::system::error_code const &ec, std::size_t) {
      if(!ec)
      {
        spdlog::debug("Done sending response");
      }
      else
      {
        spdlog::debug("async write failed: {}", ec.message());
      }
    });
}

template<typename SocketType>
void server_session_base<SocketType>::read_confirmation()
{
  boost::asio::async_read_until(
    socket_,
    stream_buffer_,
    mfsync::protocol::MFSYNC_HEADER_END,
    [me = this->shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
    {
      me->handle_read_confirmation(error, bytes_transferred);
    });
}

template<typename SocketType>
void server_session_base<SocketType>::handle_read_confirmation(boost::system::error_code const &error, std::size_t bytes_transferred)
{
  if(error)
  {
    spdlog::debug("Error during read_confirmation: {}", error.message());
    return;
  }

  boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
  std::string message(
    boost::asio::buffers_begin(bufs),
    boost::asio::buffers_begin(bufs) + bytes_transferred);

  if(message != protocol::create_begin_transmission_message())
  {
    spdlog::debug("begin transmission wasnt confirmed. aborting");
    spdlog::debug("message was: {}", message);
    return;
  }

  auto source_file = file_handler_.read_file(requested_.file_info);

  if(!source_file.has_value())
  {
    spdlog::error("Cant read file");
    //handle_error();
    return;
  }

  ifstream_ = std::move(source_file.value());

  ifstream_.seekg(0, ifstream_.end);
  const auto file_size = ifstream_.tellg();
  ifstream_.seekg(requested_.offset, ifstream_.beg);

  spdlog::debug("Start sending file: {} with size: {}", requested_.file_info.file_name, file_size);

  if(bar_ == nullptr)
  {
    bar_ = progress_->create_file_progress(requested_.file_info);
    bar_->status = progress::STATUS::UPLOADING;
  }

  writebuf_.resize(requested_.chunksize);
  write_file();
}

template<typename SocketType>
void server_session_base<SocketType>::write_file()
{
  if(!ifstream_)
  {
    bar_->bytes_transferred = requested_.file_info.size;
    bar_->status = progress::STATUS::DONE;
    bar_ = nullptr;
    spdlog::debug("Done snding file.");
    return;
  }

  ifstream_.read(writebuf_.data(), writebuf_.size());

  bar_->bytes_transferred = ifstream_.tellg();

  if(ifstream_.fail() && !ifstream_.eof())
  {
    spdlog::debug("Failed reading file");
    //handle_error();
    return;
  }


  async_write(socket_,
    boost::asio::buffer(writebuf_.data(), writebuf_.size()),
    [me = this->shared_from_this()](boost::system::error_code const &ec, std::size_t) {
      if(!ec)
      {
        me->write_file();
      }
      else
      {
        spdlog::debug("async write failed: {}", ec.message());
      }
    });
}

} //closing namespace mfsync::filetransfer
