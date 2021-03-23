#pragma once

#include <deque>
#include <memory>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/file_handler.h"
#include "mfsync/protocol.h"
#include "mfsync/deque.h"

namespace mfsync
{


namespace filetransfer
{

class server_session : public std::enable_shared_from_this<server_session>
{
public:
  server_session(boost::asio::io_context& context,
                 mfsync::file_handler& handler)
    : io_context_(context)
    , socket_(context)
    , file_handler_(handler)
  {}

  boost::asio::ip::tcp::socket& get_socket()
  {
    return socket_;
  }

  void read()
  {
    boost::asio::async_read_until(
      socket_,
      stream_buffer_,
      mfsync::protocol::MFSYNC_HEADER_END,
      [me = shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
      {
        me->handle_read_header(error, bytes_transferred);
      });
  }

  void handle_read_header(boost::system::error_code const &error, std::size_t bytes_transferred)
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

  void send_confirmation()
  {
    message_ = protocol::create_begin_transmission_message();
    spdlog::debug("Sending response: {}", message_);
    async_write(socket_,
      boost::asio::buffer(message_.data(), message_.size()),
      [me = shared_from_this()](boost::system::error_code const &ec, std::size_t) {
        if(!ec)
        {
          spdlog::info("Done sending response");
          me->read_confirmation();
        }
        else
        {
          spdlog::info("async write failed: {}", ec.message());
        }
      });
  }

  void reply_with_error(const std::string& reason)
  {
    message_ = protocol::create_error_message(reason);
    spdlog::debug("Sending response: {}", message_);
    async_write(socket_,
      boost::asio::buffer(message_.data(), message_.size()),
      [me = shared_from_this()](boost::system::error_code const &ec, std::size_t) {
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

  void read_confirmation()
  {
    boost::asio::async_read_until(
      socket_,
      stream_buffer_,
      mfsync::protocol::MFSYNC_HEADER_END,
      [me = shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
      {
        me->handle_read_confirmation(error, bytes_transferred);
      });
  }

  void handle_read_confirmation(boost::system::error_code const &error, std::size_t bytes_transferred)
  {
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

    spdlog::info("Start sending file: {}", requested_.file_info.file_name);
    spdlog::debug("FileSize: {}, Sha256sum: {}", file_size, requested_.file_info.sha256sum);
    write_file();
  }

  void write_file()
  {
    if(!ifstream_)
    {
      return;
    }

    writebuf_.resize(requested_.chunksize);
    ifstream_.read(writebuf_.data(), writebuf_.size());

    if(ifstream_.fail() && !ifstream_.eof())
    {
      spdlog::debug("Failed reading file");
      //handle_error();
      return;
    }

    async_write(socket_,
      boost::asio::buffer(writebuf_.data(), writebuf_.size()),
      [me = shared_from_this()](boost::system::error_code const &ec, std::size_t) {
        if(!ec)
        {
          spdlog::debug("sent chunk, sending next");
          me->write_file();
        }
        else
        {
          spdlog::info("async write failed: {}", ec.message());
        }
      });
  }


private:
  boost::asio::io_context& io_context_;
  boost::asio::ip::tcp::socket socket_;
  mfsync::file_handler& file_handler_;
  std::string message_;
  requested_file requested_;
  boost::asio::streambuf stream_buffer_;
  std::vector<char> writebuf_;
  std::ifstream ifstream_;
};

class client_session : public std::enable_shared_from_this<client_session>
{
public:
  client_session() = delete;
  client_session(boost::asio::io_context& context,
                 available_file file,
                 mfsync::concurrent::deque<available_file>* deque,
                 mfsync::file_handler& handler)
    : io_context_(context)
    , socket_(context)
    , available_(file)
    , deque_(deque)
    , file_handler_(handler)
  {}

  boost::asio::ip::tcp::socket& get_socket()
  {
    return socket_;
  }

  void start_request()
  {
    //auto endpoint = boost::asio::ip::tcp::endpoint{available_.source_address, available_.source_port};
    boost::asio::ip::tcp::resolver resolver{io_context_};
    auto endpoint = resolver.resolve(available_.source_address.to_string(),
                                     std::to_string(available_.source_port));

    boost::asio::async_connect(
      socket_,
      endpoint,
      [this, me = shared_from_this()]
      (boost::system::error_code ec, boost::asio::ip::tcp::endpoint)
      {
        if(!ec)
        {
          me->request_file();
        }
        else
        {
          spdlog::debug("Couldnt conntect. error: {}", ec.message());
          spdlog::debug("Target host: {} {}", available_.source_address.to_string(),
                                              available_.source_port);
        }
      });
  }

  void request_file()
  {
    //not setting offset here, it will be set by file_handler when file is created
    requested_file requested;
    requested.file_info = std::move(available_.file_info);
    requested.chunksize = mfsync::protocol::CHUNKSIZE;

    auto output_file_stream = file_handler_.create_file(requested);

    if(!output_file_stream.has_value())
    {
      spdlog::debug("file creation failed. abort session");
      handle_error();
      return;
    }

    ofstream_ = std::move(output_file_stream.value());
    requested_ = requested;

    message_ = protocol::create_message_from_requested_file(requested);

    spdlog::debug("Sending message: {}", message_);

    async_write(socket_,
      boost::asio::buffer(message_.data(), message_.size()),
      [me = shared_from_this()](boost::system::error_code const &ec, std::size_t) {
        if(!ec)
        {
          spdlog::info("Done requesting file!");
          me->read_file_request_response();
        }
        else
        {
          spdlog::info("async write failed: {}", ec.message());
        }
      });
  }

  void read_file_request_response()
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

  void handle_read_file_request_response(boost::system::error_code const &error, std::size_t bytes_transferred)
  {
    if(!error)
    {
      boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
      std::string line(
        boost::asio::buffers_begin(bufs),
        boost::asio::buffers_begin(bufs) + bytes_transferred);;

      spdlog::debug("Received file_request_response: {}", line);

      if(line != protocol::create_begin_transmission_message())
      {
        spdlog::debug("Server returned with error: {}", line);
        handle_error();
        return;
      }

      message_ = protocol::create_begin_transmission_message();
      spdlog::debug("Sending response: {}", message_);
      bytes_written_to_requested_ = requested_.offset;

      async_write(socket_,
        boost::asio::buffer(message_.data(), message_.size()),
        [me = shared_from_this()](boost::system::error_code const &ec, std::size_t) {
          if(!ec)
          {
            spdlog::info("Done sending response");
            me->read_file_chunk();
          }
          else
          {
            spdlog::info("async write failed: {}", ec.message());
            me->handle_error();
          }
        });
    }
    else
    {
      spdlog::debug("Error on handle_read_file_request_response: {}", error.message());
    }
  }

  void read_file_chunk()
  {
    readbuf_.resize(requested_.chunksize);

    auto bytes_left = requested_.file_info.size - bytes_written_to_requested_;

    if(bytes_left <= 0)
    {
      spdlog::info("complete file is written!");
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


  void handle_read_file_chunk(boost::system::error_code const &error, std::size_t bytes_transferred)
  {
    ofstream_.write(reinterpret_cast<char*>(readbuf_.data()), bytes_transferred, bytes_written_to_requested_);
    bytes_written_to_requested_ += bytes_transferred;

    spdlog::debug("received. bytes written: {} of: {}", ofstream_.tellp(), requested_.file_info.size);
    if(ofstream_.tellp() < static_cast<std::streamsize>(requested_.file_info.size))
    {
      read_file_chunk();
      return;
    }

    spdlog::info("received file {}", requested_.file_info.file_name);
    spdlog::info("with size in mb: {}", static_cast<double>(requested_.file_info.size / 1048576.0));

    ofstream_.flush();

    if(!file_handler_.finalize_file(requested_.file_info))
    {
      spdlog::error("finalizing failed!!!");
      handle_error();
    }
  }

  void handle_error()
  {
    spdlog::error("handle_error not implemented yet!!!");
  }

private:
  boost::asio::io_context& io_context_;
  boost::asio::ip::tcp::socket socket_;
  available_file available_;
  requested_file requested_;
  size_t bytes_written_to_requested_ = 0;
  mfsync::concurrent::deque<available_file>* deque_ = nullptr;
  mfsync::file_handler& file_handler_;
  std::string message_;
  boost::asio::streambuf stream_buffer_;
  std::vector<uint8_t> readbuf_;
  mfsync::ofstream_wrapper ofstream_;
};

class server
{
public:
  server(boost::asio::io_context &io_context, unsigned short port, mfsync::file_handler& file_handler)
    : io_context_(io_context)
    , acceptor_(io_context_)
    , port_(port)
    , file_handler_(file_handler)
  {}

  void run()
  {
    start_listening(port_);
    accept_connections();
  }

  void stop()
  {
    acceptor_.close();
    spdlog::debug("[Server] closed acceptor");
  }

private:

  void start_listening(uint16_t port)
  {
    spdlog::debug("setting up endpoint");
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), port);
    spdlog::debug("setting port to {}", port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    spdlog::debug("started listening");
  }

  void accept_connections()
  {
    auto handler = std::make_shared<mfsync::filetransfer::server_session>(io_context_, file_handler_);
    acceptor_.async_accept(handler->get_socket(), [this, handler](auto ec) { handle_new_connection(handler, ec); });
  }

  void handle_new_connection(std::shared_ptr<mfsync::filetransfer::server_session> handler,
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

    handler->read();

    auto new_handler = std::make_shared<mfsync::filetransfer::server_session>(io_context_, file_handler_);

    acceptor_.async_accept(new_handler->get_socket(),
                           [this, new_handler](auto ec) { handle_new_connection(new_handler, ec); });
  }

  boost::asio::io_context &io_context_;
  boost::asio::ip::tcp::acceptor acceptor_;
  unsigned short port_;
  mfsync::file_handler& file_handler_;
};

}

class file_receive_handler
{
public:
  file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler);

  file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler,
                       std::vector<std::string> files_to_request);

  void set_files(std::vector<std::string> files_to_request);
  void request_all_files();
  void get_files();

private:

  void try_start_new_session();
  void request_file(available_file file);
  void wait_for_new_files();
  void handle_timeout(const boost::system::error_code& error);

  boost::asio::io_context& io_context_;
  boost::asio::deadline_timer timer_;
  mfsync::file_handler& file_handler_;
  std::vector<std::string> files_to_request_;
  mfsync::concurrent::deque<available_file> request_queue_;
  bool request_all_;
  std::weak_ptr<mfsync::filetransfer::client_session> session_;
  mutable std::mutex mutex_;
};

} //closing namespace mfsync
