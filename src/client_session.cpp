#include "mfsync/client_session.h"

#include <boost/bind.hpp>
#include "spdlog/spdlog.h"
#include "mfsync/protocol.h"

namespace mfsync::filetransfer
{

template<typename SocketType>
client_session_base<SocketType>::client_session_base(boost::asio::io_context& context,
                                                     SocketType socket,
                                                     mfsync::concurrent::deque<available_file>& deque,
                                                     mfsync::file_handler& handler)
  : io_context_(context)
  , socket_(std::move(socket))
  , deque_(deque)
  , file_handler_(handler)
{}

client_session::client_session(boost::asio::io_context& context,
                               mfsync::concurrent::deque<available_file>& deque,
                               mfsync::file_handler& handler)
  : client_session_base<boost::asio::ip::tcp::socket>(context, boost::asio::ip::tcp::socket{context}, deque, handler)
{}

client_tls_session::client_tls_session(boost::asio::io_context& context,
                                       boost::asio::ssl::context& ssl_context,
                                       mfsync::concurrent::deque<available_file>& deque,
                                       mfsync::file_handler& handler)
  : client_session_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(context,
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(context, ssl_context),
        deque,
        handler)
{
  socket_.set_verify_mode(boost::asio::ssl::verify_peer);
  socket_.set_verify_callback(std::bind(&client_tls_session::verify_certificate,
                                        this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
}

bool client_tls_session::verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx)
{
  // The verify callback can be used to check whether the certificate that is
  // being presented is valid for the peer. For example, RFC 2818 describes
  // the steps involved in doing this for HTTPS. Consult the OpenSSL
  // documentation for more details. Note that the callback is called once
  // for each certificate in the certificate chain, starting from the root
  // certificate authority.

  // In this example we will simply print the certificate's subject name.
  char subject_name[256];
  X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
  X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
  spdlog::debug("Verifying {}", subject_name);

  return preverified;
}

void client_tls_session::handshake()
{
  //todo: async_handshake waits forever. if the server doesnt support tls the handshake will never finish
  //eventually cancel it like in
  //https://stackoverflow.com/questions/40026440/boost-asio-async-handshake-cannot-be-canceled
  spdlog::debug("Starting handshake");
  socket_.async_handshake(boost::asio::ssl::stream_base::client,
      [this, me = base::shared_from_this()](const boost::system::error_code& error)
      {
        if (!error)
        {
          me->request_file();
        }
        else
        {
          spdlog::error("Handshake failed: {}", error.message());
        }
      });
}

template<typename SocketType>
SocketType& client_session_base<SocketType>::get_socket()
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
    [this, me = client_session_base<boost::asio::ip::tcp::socket>::shared_from_this(), available]
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

void client_tls_session::start_request()
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
    socket_.lowest_layer(),
    endpoint,
    [this, me = this->shared_from_this(), available]
    (boost::system::error_code ec, boost::asio::ip::tcp::endpoint)
    {
      if(!ec)
      {
        std::dynamic_pointer_cast<client_tls_session>(me)->handshake();
      }
      else
      {
        spdlog::debug("Couldnt conntect. error: {}", ec.message());
        spdlog::debug("Target host: {} {}", available.value().source_address.to_string(),
                                            available.value().source_port);
      }
    });
}


template<typename SocketType>
void client_session_base<SocketType>::request_file()
{
  auto output_file_stream = file_handler_.create_file(requested_);

  if(!output_file_stream.has_value())
  {
    spdlog::debug("file creation failed. abort session");
    spdlog::debug("filename: {}, sha256sum: {}",
                  requested_.file_info.file_name, requested_.file_info.sha256sum);
    handle_error();
    return;
  }

  ofstream_ = std::move(output_file_stream.value());

  message_ = protocol::create_message_from_requested_file(requested_);

  spdlog::debug("Sending message: {}", message_);

  async_write(socket_,
    boost::asio::buffer(message_.data(), message_.size()),
    [me = this->shared_from_this()](boost::system::error_code const &ec, std::size_t) {
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

template<typename SocketType>
void client_session_base<SocketType>::read_file_request_response()
{
  boost::asio::async_read_until(
    socket_,
    stream_buffer_,
    mfsync::protocol::MFSYNC_HEADER_END,
    [me = this->shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
    {
      me->handle_read_file_request_response(error, bytes_transferred);
    });
}

template<typename SocketType>
void client_session_base<SocketType>::handle_read_file_request_response(boost::system::error_code const &error,
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
      [me = this->shared_from_this()](boost::system::error_code const &ec, std::size_t) {
        if(!ec)
        {
          spdlog::debug("Done sending response");

          if(me->bar_ == nullptr)
          {
            me->bar_ = me->progress_->create_file_progress(me->requested_.file_info) ;
            me->bar_->status = progress::STATUS::DOWNLOADING;
          }

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

template<typename SocketType>
void client_session_base<SocketType>::read_file_chunk()
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
    [me = this->shared_from_this()](boost::system::error_code const &error, std::size_t bytes_transferred)
    {
      me->handle_read_file_chunk(error, bytes_transferred);
    });
}


template<typename SocketType>
void client_session_base<SocketType>::handle_read_file_chunk(boost::system::error_code const &error,
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

  bar_->bytes_transferred = bytes_written_to_requested_;

  if(ofstream_.tellp() < static_cast<std::streamsize>(requested_.file_info.size))
  {
    read_file_chunk();
    return;
  }

  bar_->bytes_transferred = requested_.file_info.size;
  bar_->status = progress::STATUS::COMPARING;
  spdlog::debug("received file {}", requested_.file_info.file_name);
  spdlog::debug("with size in mb: {}", static_cast<double>(requested_.file_info.size / 1048576.0));

  ofstream_.flush();

  if(!file_handler_.finalize_file(requested_.file_info))
  {
    spdlog::debug("finalizing failed!!!");
    handle_error();
    return;
  }

  bar_->status = progress::STATUS::DONE;
  bar_ = nullptr;

  //spdlog::info("received file: {} - {} - {}", requested_.file_info.file_name,
  //                                            requested_.file_info.sha256sum,
  //                                            requested_.file_info.size);

  readbuf_.clear();
  //TODO: start_request currently does handshake again which leads to failing tls connection foo
  //so it is neccessary only do the deque handling setting "requested_" properly and not doing handshake anymore
  //start_request();
}

template<typename SocketType>
void client_session_base<SocketType>::handle_error()
{
  spdlog::error("handle_error not implemented yet!!!");
}

} //closing namespace mfsync::filetransfer
