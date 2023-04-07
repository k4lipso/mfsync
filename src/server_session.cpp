#include "mfsync/server_session.h"

#include <boost/bind.hpp>

#include "mfsync/protocol.h"
#include "spdlog/spdlog.h"

namespace mfsync::filetransfer {

template <typename SocketType>
server_session_base<SocketType>::server_session_base(
    SocketType socket, mfsync::file_handler& handler,
    mfsync::crypto::crypto_handler& crypto_handler)
    : socket_(std::move(socket)),
      file_handler_(handler),
      crypto_handler_(crypto_handler) {}

template <typename SocketType>
SocketType& server_session_base<SocketType>::get_socket() {
  return socket_;
}

server_session::server_session(boost::asio::ip::tcp::socket socket,
                               mfsync::file_handler& handler,
                               mfsync::crypto::crypto_handler& crypto_handler)
    : server_session_base<boost::asio::ip::tcp::socket>(
          std::move(socket), handler, crypto_handler) {
  port_ = socket_.local_endpoint().port();
}

server_tls_session::server_tls_session(
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket,
    mfsync::file_handler& handler,
    mfsync::crypto::crypto_handler& crypto_handler)
    : server_session_base<
          boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
          std::move(socket), handler, crypto_handler) {
  port_ = socket_.lowest_layer().local_endpoint().port();
}

void server_session::start() { read(); }

void server_tls_session::start() { do_handshake(); }

void server_tls_session::do_handshake() {
  socket_.async_handshake(
      boost::asio::ssl::stream_base::server,
      [this, me = shared_from_this()](const boost::system::error_code& error) {
        if (!error) {
          me->read();
        } else {
          spdlog::error("server_tls_session error on handshake: {}",
                        error.message());
        }
      });
}

template <typename SocketType>
void server_session_base<SocketType>::read() {
  boost::asio::async_read_until(
      socket_, stream_buffer_, mfsync::protocol::MFSYNC_HEADER_END,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_header(error, bytes_transferred);
      });
}

template <typename SocketType>
void server_session_base<SocketType>::handle_read_header(
    boost::system::error_code const& error, std::size_t bytes_transferred) {
  if (error) {
    spdlog::debug("Error on handle_read_header: {}", error.message());
    return;
  }

  stream_buffer_.commit(bytes_transferred);
  std::istream is(&stream_buffer_);
  std::string message(std::istreambuf_iterator<char>(is), {});
  spdlog::debug("Received header: {}", message);

  const auto type = protocol::get_message_type(message);

  if (type == protocol::type::FILE_LIST) {
    auto optional_j = protocol::get_json_from_message(message);

    if (!optional_j.has_value()) {
      return;
    }

    const auto& j = optional_j.value();
    const auto pub_key = j.at("public_key").get<std::string>();
    spdlog::debug("received init message: {}", pub_key);
    respond_encrypted(pub_key);
    return;
  }

  if (type != protocol::type::FILE) {
    spdlog::debug("received request with wrong type: {}",
                  static_cast<int>(type));
    return;
  }

  const auto result = protocol::converter<requested_file>::from_message(
      message, crypto_handler_);
  if (!result.has_value()) {
    spdlog::debug("Couldnt create requested_file from message: {}", message);
    return;
  }

  const auto& [file, pub_key] = result.value();
  public_key_ = pub_key;

  if (file_handler_.is_stored(file.file_info)) {
    requested_ = file;
    send_confirmation();
  } else {
    reply_with_error("file doesnt exists");
  }
}

template <typename SocketType>
void server_session_base<SocketType>::respond_encrypted(
    const std::string& pub_key) {
  message_ = protocol::converter<file_handler::available_files>::to_message(
      file_handler_, port_, pub_key, crypto_handler_);

  spdlog::debug("Sending response: {}", message_);
  async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  spdlog::debug("Done sending response");
                  // me->read_confirmation();
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

template <typename SocketType>
void server_session_base<SocketType>::send_confirmation() {
  message_ =
      protocol::converter<bool>::to_message(true, public_key_, crypto_handler_);

  spdlog::debug("Sending response: {}", message_);
  async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  spdlog::debug("Done sending response");
                  me->read_confirmation();
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

template <typename SocketType>
void server_session_base<SocketType>::reply_with_error(
    const std::string& reason) {
  message_ = protocol::create_error_message(reason);
  spdlog::debug("Sending response: {}", message_);
  async_write(socket_, boost::asio::buffer(message_.data(), message_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  spdlog::debug("Done sending response");
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

template <typename SocketType>
void server_session_base<SocketType>::read_confirmation() {
  boost::asio::async_read_until(
      socket_, stream_buffer_, mfsync::protocol::MFSYNC_HEADER_END,
      [me = this->shared_from_this()](boost::system::error_code const& error,
                                      std::size_t bytes_transferred) {
        me->handle_read_confirmation(error, bytes_transferred);
      });
}

template <typename SocketType>
void server_session_base<SocketType>::handle_read_confirmation(
    boost::system::error_code const& error, std::size_t bytes_transferred) {
  if (error) {
    spdlog::debug("Error during read_confirmation: {}", error.message());
    return;
  }

  boost::asio::streambuf::const_buffers_type bufs = stream_buffer_.data();
  std::string message(boost::asio::buffers_begin(bufs),
                      boost::asio::buffers_begin(bufs) + bytes_transferred);

  const auto got_accepted = protocol::converter<bool>::from_message(
      message, public_key_, crypto_handler_);

  if (!got_accepted.has_value() || !got_accepted.value()) {
    spdlog::debug("begin transmission wasnt confirmed. aborting");
    spdlog::debug("message was: {}", message);
    return;
  }

  auto source_file = file_handler_.read_file(requested_.file_info);

  if (!source_file.has_value()) {
    spdlog::error("Cant read file");
    // handle_error();
    return;
  }

  ifstream_ = std::move(source_file.value());

  ifstream_.seekg(0, ifstream_.end);
  const auto file_size = ifstream_.tellg();
  ifstream_.seekg(requested_.offset, ifstream_.beg);

  spdlog::debug("Start sending file: {} with size: {}",
                requested_.file_info.file_name, file_size);

  if (bar_ == nullptr) {
    bar_ = progress_->create_file_progress(requested_.file_info);
    bar_->status = progress::STATUS::UPLOADING;
  }

  writebuf_.resize(requested_.chunksize);
  write_file();
}

template <typename SocketType>
void server_session_base<SocketType>::write_file() {
  if (!ifstream_) {
    bar_->bytes_transferred = requested_.file_info.size;
    bar_->status = progress::STATUS::DONE;
    bar_ = nullptr;
    spdlog::debug("Done sending file.");
    return;
  }

  // ifstream_.read(reinterpret_cast<char*>(writebuf_.data()),
  // writebuf_.size());
  writebuf_.clear();
  crypto_handler_.encrypt_file_to_buf(public_key_, ifstream_,
                                      requested_.chunksize, writebuf_);

  bar_->bytes_transferred = ifstream_.tellg();

  if (ifstream_.fail() && !ifstream_.eof()) {
    spdlog::debug("Failed reading file");
    // handle_error();
    return;
  }

  spdlog::debug("Writing {} bytes.", writebuf_.size());
  async_write(socket_, boost::asio::buffer(writebuf_.data(), writebuf_.size()),
              [me = this->shared_from_this()](
                  boost::system::error_code const& ec, std::size_t) {
                if (!ec) {
                  me->write_file();
                } else {
                  spdlog::debug("async write failed: {}", ec.message());
                }
              });
}

}  // namespace mfsync::filetransfer
