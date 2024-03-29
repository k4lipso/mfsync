#pragma once

#include <deque>
#include <memory>

#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/file_handler.h"
#include "mfsync/protocol.h"
#include "mfsync/deque.h"
#include "mfsync/client_session.h"
#include "mfsync/crypto.h"

namespace mfsync
{

class file_receive_handler
{
public:
  file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler,
                       size_t max_concurrent_sessions,
                       mfsync::crypto::crypto_handler& crypto_handler,
                       mfsync::filetransfer::progress_handler* progress);

  file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler,
                       size_t max_concurrent_sessions,
                       mfsync::crypto::crypto_handler& crypto_handler,
                       mfsync::filetransfer::progress_handler* progress,
                       std::vector<std::string> files_to_request);

  void set_files(std::vector<std::string> files_to_request);
  void request_all_files();
  void get_files();
  std::future<void> get_future();

  void enable_tls(const std::string& cert_file);

protected:
  void fill_request_queue();
  mfsync::concurrent::deque<available_file> request_queue_;
private:

  void start_new_session();
  void add_to_request_queue(available_file file);
  void wait();
  void handle_timeout(const boost::system::error_code& error);

  boost::asio::io_context& io_context_;
  boost::asio::deadline_timer timer_;
  mfsync::file_handler& file_handler_;
  mfsync::crypto::crypto_handler& crypto_handler_;
  std::vector<std::string> files_to_request_;
  std::optional<boost::asio::ssl::context> ctx_;
  bool request_all_;
  std::vector<std::weak_ptr<mfsync::filetransfer::session_base>> sessions_;
  std::promise<void> promise_;
  mutable std::mutex mutex_;
  mfsync::filetransfer::progress_handler* progress_;

};

} //closing namespace mfsync
