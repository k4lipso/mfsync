#include "mfsync/client_session.h"

#include <boost/bind.hpp>

#include "mfsync/protocol.h"
#include "spdlog/spdlog.h"

namespace mfsync::filetransfer {

template <typename SocketType>
client_encrypted_session<SocketType>::client_encrypted_session(
    boost::asio::io_context& context, SocketType socket,
    mfsync::file_handler& handler,
    mfsync::crypto::crypto_handler& crypto_handler,
    mfsync::host_information host_info)
    : io_context_(context),
      socket_(std::move(socket)),
      file_handler_(handler),
      crypto_handler_(crypto_handler),
      host_info_(std::move(host_info)) {}

client_encrypted_file_list::client_encrypted_file_list(
    boost::asio::io_context& context, mfsync::file_handler& handler,
    mfsync::crypto::crypto_handler& crypto_handler,
    mfsync::host_information host_info)
    : client_encrypted_session<boost::asio::ip::tcp::socket>(
          context, boost::asio::ip::tcp::socket{context}, handler,
          crypto_handler, std::move(host_info)) {}

template <typename SocketType>
void client_encrypted_session<SocketType>::initialize_communication() {
  const auto salt = crypto_handler_.encode(crypto_handler_.generate_salt());
  derived_crypto_handler_ = crypto_handler_.derive(host_info_.public_key, salt);

  if(!derived_crypto_handler_) {
      spdlog::error("Could not derive cryptohandler. key: {}, salt: {}", host_info_.public_key, salt);
      return;
  }

  message_ =
      protocol::create_handshake_message(derived_crypto_handler_->get_public_key(), salt);
  spdlog::trace("Sending message: {}", message_);

  async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  spdlog::debug("Done sending init message");
                  me->read_handshake();
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

template <typename SocketType>
void client_encrypted_session<SocketType>::read_handshake() {
  boost::asio::async_read_until(
      socket_, stream_buffer_, mfsync::protocol::MFSYNC_HEADER_END,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_handshake(error, bytes_transferred);
      });
}

template <typename SocketType>
void client_encrypted_session<SocketType>::handle_read_handshake(
    boost::system::error_code const& error, std::size_t bytes_transferred) {
  if (error) {
    spdlog::debug("Error on handle_read_file_request_response: {}",
                  error.message());
    return;
  }

  boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
  std::string response_message(
      boost::asio::buffers_begin(bufs),
      boost::asio::buffers_begin(bufs) + bytes_transferred);

  stream_buffer_.consume(bytes_transferred);

  spdlog::trace("Received encrypted response: {}", response_message);

  const auto got_accepted = protocol::converter<bool>::from_message(
      response_message, host_info_.public_key, *derived_crypto_handler_.get());

  if (!got_accepted) {
    spdlog::debug("Handshake got denied");
    return;
  }

  message_ =
      protocol::create_file_list_message(derived_crypto_handler_->get_public_key());
  spdlog::trace("Sending message: {}", message_);

  async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  spdlog::debug("Done sending init message");
                  me->read_encrypted_response();
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

template <typename SocketType>
void client_encrypted_session<SocketType>::read_encrypted_response() {
  boost::asio::async_read_until(
      socket_, stream_buffer_, mfsync::protocol::MFSYNC_HEADER_END,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_encrypted_response(error, bytes_transferred);
      });
}

template <typename SocketType>
void client_encrypted_session<SocketType>::handle_read_encrypted_response(
    boost::system::error_code const& error, std::size_t bytes_transferred) {
  if (error) {
    spdlog::debug("Error on handle_read_file_request_response: {}",
                  error.message());
    return;
  }

  boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
  std::string response_message(
      boost::asio::buffers_begin(bufs),
      boost::asio::buffers_begin(bufs) + bytes_transferred);

  stream_buffer_.consume(bytes_transferred);

  spdlog::trace("Received encrypted response: {}", response_message);

  auto available =
      protocol::converter<mfsync::file_handler::available_files>::from_message(
      response_message, host_info_.public_key, *derived_crypto_handler_.get(),
          socket_.remote_endpoint(), true);

  if (available.has_value()) {
    file_handler_.add_available_files(std::move(available.value()));
  }
}

void client_encrypted_file_list::start_request() {
  boost::asio::ip::tcp::resolver resolver{io_context_};
  auto endpoint =
      resolver.resolve(host_info_.ip, std::to_string(host_info_.port));

  boost::asio::async_connect(
      socket_, endpoint,
      [this, me = client_encrypted_session<
                 boost::asio::ip::tcp::socket>::shared_from_this()](
          boost::system::error_code ec, boost::asio::ip::tcp::endpoint) {
        if (!ec) {
          me->initialize_communication();
        } else {
          spdlog::debug("Couldnt conntect. error: {}", ec.message());
          spdlog::debug("Target host: {} {}", host_info_.ip, host_info_.port);
        }
      });
}

template <typename SocketType>
client_session_base<SocketType>::client_session_base(
    boost::asio::io_context& context, SocketType socket,
    mfsync::concurrent::deque<available_file>& deque,
    mfsync::file_handler& handler,
    mfsync::crypto::crypto_handler& crypto_handler)
    : io_context_(context),
      socket_(std::move(socket)),
      deque_(deque),
      file_handler_(handler),
      crypto_handler_(crypto_handler) {}

client_session::client_session(boost::asio::io_context& context,
                               mfsync::concurrent::deque<available_file>& deque,
                               mfsync::file_handler& handler,
                               mfsync::crypto::crypto_handler& crypto_handler)
    : client_session_base<boost::asio::ip::tcp::socket>(
          context, boost::asio::ip::tcp::socket{context}, deque, handler,
          crypto_handler) {}

client_tls_session::client_tls_session(
    boost::asio::io_context& context, boost::asio::ssl::context& ssl_context,
    mfsync::concurrent::deque<available_file>& deque,
    mfsync::file_handler& handler,
    mfsync::crypto::crypto_handler& crypto_handler)
    : client_session_base<
          boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
          context,
          boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(context,
                                                                 ssl_context),
          deque, handler, crypto_handler) {
  socket_.set_verify_mode(boost::asio::ssl::verify_peer);
  socket_.set_verify_callback(std::bind(&client_tls_session::verify_certificate,
                                        this, std::placeholders::_1,
                                        std::placeholders::_2));
}

bool client_tls_session::verify_certificate(
    bool preverified, boost::asio::ssl::verify_context& ctx) {
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

void client_tls_session::handshake() {
  // todo: async_handshake waits forever. if the server doesnt support tls the
  // handshake will never finish eventually cancel it like in
  // https://stackoverflow.com/questions/40026440/boost-asio-async-handshake-cannot-be-canceled
  spdlog::debug("Starting handshake");
  socket_.async_handshake(
      boost::asio::ssl::stream_base::client,
      [this,
       me = base::shared_from_this()](const boost::system::error_code& error) {
        if (!error) {
          me->request_file();
        } else {
          spdlog::error("Handshake failed: {}", error.message());
        }
      });
}

template <typename SocketType>
SocketType& client_session_base<SocketType>::get_socket() {
  return socket_;
}

void client_session::start_request() {
  auto available = deque_.try_pop();

  if (!available.has_value()) {
    return;
  }

  // not setting offset here, it will be set by file_handler when file is
  // created
  requested_.file_info = std::move(available.value().file_info);
  pub_key_ = available.value().public_key;
  requested_.chunksize = mfsync::protocol::CHUNKSIZE;

  boost::asio::ip::tcp::resolver resolver{io_context_};
  auto endpoint =
      resolver.resolve(available.value().source_address.to_string(),
                       std::to_string(available.value().source_port));

  boost::asio::async_connect(
      socket_, endpoint,
      [this,
       me = client_session_base<
           boost::asio::ip::tcp::socket>::shared_from_this(),
       available](boost::system::error_code ec,
                  boost::asio::ip::tcp::endpoint) {
        if (!ec) {
          me->initialize_communication();
        } else {
          spdlog::debug("Couldnt conntect. error: {}", ec.message());
          spdlog::debug("Target host: {} {}",
                        available.value().source_address.to_string(),
                        available.value().source_port);
        }
      });
}

void client_tls_session::start_request() {
  auto available = deque_.try_pop();

  if (!available.has_value()) {
    return;
  }

  // not setting offset here, it will be set by file_handler when file is
  // created
  requested_.file_info = std::move(available.value().file_info);
  requested_.chunksize = mfsync::protocol::CHUNKSIZE;

  boost::asio::ip::tcp::resolver resolver{io_context_};
  auto endpoint =
      resolver.resolve(available.value().source_address.to_string(),
                       std::to_string(available.value().source_port));

  boost::asio::async_connect(
      socket_.lowest_layer(), endpoint,
      [this, me = this->shared_from_this(), available](
          boost::system::error_code ec, boost::asio::ip::tcp::endpoint) {
        if (!ec) {
          std::dynamic_pointer_cast<client_tls_session>(me)->handshake();
        } else {
          spdlog::debug("Couldnt conntect. error: {}", ec.message());
          spdlog::debug("Target host: {} {}",
                        available.value().source_address.to_string(),
                        available.value().source_port);
        }
      });
}

template <typename SocketType>
void client_session_base<SocketType>::initialize_communication() {
  const auto salt = crypto_handler_.encode(crypto_handler_.generate_salt());
  derived_crypto_handler_ = crypto_handler_.derive(pub_key_, salt);

  if(!derived_crypto_handler_) {
      spdlog::error("Could not derive cryptohandler. key: {}, salt: {}", pub_key_, salt);
      return;
  }

  message_ =
      protocol::create_handshake_message(derived_crypto_handler_->get_public_key(), salt);
  spdlog::trace("Sending message: {}", message_);

  async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  spdlog::debug("Done sending init message");
                  me->read_handshake();
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

template <typename SocketType>
void client_session_base<SocketType>::read_handshake() {
  boost::asio::async_read_until(
      socket_, stream_buffer_, mfsync::protocol::MFSYNC_HEADER_END,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_handshake(error, bytes_transferred);
      });
}

template <typename SocketType>
void client_session_base<SocketType>::handle_read_handshake(
    boost::system::error_code const& error, std::size_t bytes_transferred) {
  if (error) {
    spdlog::debug("Error on handle_read_file_request_response: {}",
                  error.message());
    return;
  }

  boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
  std::string response_message(
      boost::asio::buffers_begin(bufs),
      boost::asio::buffers_begin(bufs) + bytes_transferred);

  stream_buffer_.consume(bytes_transferred);

  spdlog::trace("Received encrypted response: {}", response_message);

  const auto got_accepted = protocol::converter<bool>::from_message(
      response_message, pub_key_, *derived_crypto_handler_.get());

  if (!got_accepted) {
    spdlog::debug("Handshake got denied");
    return;
  }

  request_file();
  //message_ =
  //    protocol::create_file_list_message(derived_crypto_handler_->get_public_key(), salt);
  //spdlog::trace("Sending message: {}", message_);

  //async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
  //            [me = this->shared_from_this()](
  //                boost::system::error_code const& ec, std::size_t) {
  //              if (!ec) {
  //                spdlog::debug("Done sending init message");
  //                me->read_encrypted_response();
  //              } else {
  //                spdlog::debug("async write failed: {}", ec.message());
  //              }
  //            });
}

template<typename SocketType>
void client_session_base<SocketType>::read_encrypted_response(){
  boost::asio::async_read_until(
      socket_, stream_buffer_, mfsync::protocol::MFSYNC_HEADER_END,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_encrypted_response(error, bytes_transferred);
      });

}

template<typename SocketType>
void client_session_base<SocketType>::handle_read_encrypted_response(boost::system::error_code const &error,
                                                                     std::size_t bytes_transferred){
  if (error) {
    spdlog::debug("Error on handle_read_file_request_response: {}",
                  error.message());
    return;
  }

  boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
  std::string response_message(
      boost::asio::buffers_begin(bufs),
      boost::asio::buffers_begin(bufs) + bytes_transferred);

  stream_buffer_.consume(bytes_transferred);

  spdlog::trace("Received encrypted response: {}", response_message);

  auto available =
      protocol::converter<mfsync::file_handler::available_files>::from_message(
          response_message, pub_key_, crypto_handler_,
          socket_.remote_endpoint(), true);

  if (available.has_value()) {
    file_handler_.add_available_files(std::move(available.value()));
  }

}


template <typename SocketType>
void client_session_base<SocketType>::request_file() {
  auto output_file_stream = file_handler_.create_file(requested_);

  if (!output_file_stream.has_value()) {
    spdlog::debug("file creation failed. abort session");
    spdlog::debug("filename: {}", requested_.file_info.file_name);
    handle_error();
    return;
  }

  ofstream_ = std::move(output_file_stream.value());

  message_ = protocol::converter<requested_file>::to_message(
      requested_, pub_key_, *derived_crypto_handler_.get());

  spdlog::debug("Sending message: {}", message_);

  async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  spdlog::debug("Done requesting file!");
                  me->read_file_request_response();
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

template <typename SocketType>
void client_session_base<SocketType>::read_file_request_response() {
  boost::asio::async_read_until(
      socket_, stream_buffer_, mfsync::protocol::MFSYNC_HEADER_END,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_file_request_response(error, bytes_transferred);
      });
}

template <typename SocketType>
void client_session_base<SocketType>::handle_read_file_request_response(
    boost::system::error_code const& error, std::size_t bytes_transferred) {
  if (!error) {
    boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
    std::string response_message(
        boost::asio::buffers_begin(bufs),
        boost::asio::buffers_begin(bufs) + bytes_transferred);

    stream_buffer_.consume(bytes_transferred);

    spdlog::debug("Received encrypted response: {}", response_message);

    const auto got_accepted = protocol::converter<bool>::from_message(
        response_message, pub_key_, *derived_crypto_handler_.get());

    if (!got_accepted.has_value() || !got_accepted.value()) {
      spdlog::debug("file list request got denied by host {}.", pub_key_);
      return;
    }

    message_ =
        protocol::converter<bool>::to_message(true, pub_key_, *derived_crypto_handler_.get());

    spdlog::debug("Sending response: {}", message_);

    bytes_written_to_requested_ = requested_.offset;

    async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
                [me = this->shared_from_this()](
                    boost::system::error_code const& ec, std::size_t) {
                  if (!ec) {
                    spdlog::debug("Done sending response");

                    if (me->bar_ == nullptr) {
                      me->bar_ = me->progress_->create_file_progress(
                          me->requested_.file_info);
                      me->bar_->status = progress::STATUS::DOWNLOADING;
                    }

                    me->readbuf_.resize(me->requested_.chunksize);
                    me->read_file_chunk();
                  } else {
                    spdlog::debug("async write failed: {}", ec.message());
                    me->handle_error();
                  }
                });
  } else {
    spdlog::debug("Error on handle_read_file_request_response: {}",
                  error.message());
  }
}

template <typename SocketType>
void client_session_base<SocketType>::read_file_chunk() {
  spdlog::debug("READ FILE CHUNK");
  auto bytes_left = requested_.file_info.size - bytes_written_to_requested_;

  if (bytes_left <= 0) {
    spdlog::debug("complete file is written!");
    return;
  }

  if (bytes_left > requested_.chunksize) {
    bytes_left = requested_.chunksize;
  }

  spdlog::debug("Trying read buffer with {} bytes", bytes_left);
  boost::asio::mutable_buffers_1 buf =
      boost::asio::buffer(&readbuf_[0], bytes_left /*+ 12 /* TAG_SIZE*/);
  boost::asio::async_read(
      socket_, buf,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_file_chunk(error, bytes_transferred);
      });
}

template <typename SocketType>
void client_session_base<SocketType>::handle_read_file_chunk(
    boost::system::error_code const& error, std::size_t bytes_transferred) {
  if (error) {
    spdlog::debug("error during read_file_chunk: {}", error.message());
    handle_error();
    return;
  }

  spdlog::debug("Received {} bytes", bytes_transferred);
  auto pump_all = (bytes_written_to_requested_ + bytes_transferred) >
                  requested_.file_info.size;

  ofstream_.get_ofstream().seekp(bytes_written_to_requested_);
  derived_crypto_handler_->decrypt_file_to_buf(pub_key_, ofstream_.get_ofstream(),
                                      bytes_transferred, readbuf_, pump_all);
  ofstream_.get_ofstream().flush();
  // ofstream_.write(reinterpret_cast<char*>(readbuf_.data()),
  // bytes_transferred,
  //                 bytes_written_to_requested_);
  bytes_written_to_requested_ += bytes_transferred;

  if (ofstream_.tellp() <
      static_cast<std::streamsize>(requested_.file_info.size)) {
    bar_->bytes_transferred = bytes_written_to_requested_;
    read_file_chunk();
    return;
  }

  bar_->status = progress::STATUS::COMPARING;
  spdlog::debug("received file {}", requested_.file_info.file_name);
  spdlog::debug("with size in mb: {}",
                static_cast<double>(requested_.file_info.size / 1048576.0));

  ofstream_.flush();

  if (!file_handler_.finalize_file(requested_.file_info)) {
    spdlog::debug("finalizing failed!!!");
    handle_error();
    return;
  }

  bar_->bytes_transferred = requested_.file_info.size;
  bar_->status = progress::STATUS::DONE;
  bar_ = nullptr;

  // spdlog::info("received file: {} - {} - {}",
  // requested_.file_info.file_name,
  //                                             requested_.file_info.sha256sum,
  //                                             requested_.file_info.size);

  readbuf_.clear();
  // TODO: start_request currently does handshake again which leads to failing
  // tls connection foo so it is neccessary only do the deque handling setting
  // "requested_" properly and not doing handshake anymore start_request();
}

template <typename SocketType>
void client_session_base<SocketType>::handle_error() {
  spdlog::debug("handle_error not implemented yet!!!");
}

}  // namespace mfsync::filetransfer
