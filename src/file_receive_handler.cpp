#include "mfsync/file_receive_handler.h"

#include <boost/bind.hpp>

namespace mfsync
{

file_receive_handler::file_receive_handler(boost::asio::io_context& context,
                                           mfsync::file_handler& file_handler,
                                           size_t max_concurrent_sessions,
                                           filetransfer::progress_handler* progress)
  : io_context_(context)
  , timer_(context)
  , file_handler_(file_handler)
  , request_all_{true}
  , sessions_{max_concurrent_sessions}
  , progress_(progress)
{
}

file_receive_handler::file_receive_handler(boost::asio::io_context& context,
                                           mfsync::file_handler& file_handler,
                                           size_t max_concurrent_sessions,
                                           filetransfer::progress_handler* progress,
                                           std::vector<std::string> files_to_request)
  : io_context_(context)
  , timer_(context)
  , file_handler_(file_handler)
  , files_to_request_(std::move(files_to_request))
  , request_all_{false}
  , sessions_{max_concurrent_sessions}
  , progress_(progress)
{
}

void file_receive_handler::set_files(std::vector<std::string> files_to_request)
{
  request_all_ = false;
  files_to_request_ = std::move(files_to_request);
}

void file_receive_handler::request_all_files()
{
  files_to_request_.clear();
  request_all_ = true;
}

void file_receive_handler::get_files()
{
  std::scoped_lock lk{mutex_};

  if(std::none_of(sessions_.begin(), sessions_.end(), [](const auto& session_ptr)
    { return session_ptr.expired();}))
  {
    wait();
    return;
  }

  auto availables = file_handler_.get_available_files();

  if(request_all_)
  {
    for(auto& available : availables)
    {
      add_to_request_queue(std::move(available));
    }

    if(request_queue_.empty())
    {
      wait();
      return;
    }

    start_new_session();
    wait();
    return;
  }

  for(auto& sha256sum : files_to_request_)
  {
    auto it = availables.find(sha256sum);

    if(it == availables.end())
    {
      continue;
    }

    add_to_request_queue(*it);
  }

  //clean files_to_request
  files_to_request_.erase(std::remove_if(files_to_request_.begin(), files_to_request_.end(),
                      [this](const auto& sha256sum){ return file_handler_.is_stored(sha256sum); }),
                      files_to_request_.end());

  if(files_to_request_.empty())
  {
    promise_.set_value();
  }

  start_new_session();
  wait();
}

std::future<void> file_receive_handler::get_future()
{
  return promise_.get_future();
}

void file_receive_handler::enable_tls(const std::string& cert_file)
{
  ctx_ = boost::asio::ssl::context{boost::asio::ssl::context::sslv23};
  ctx_.value().load_verify_file(cert_file);
}

void file_receive_handler::start_new_session()
{
  std::for_each(sessions_.begin(), sessions_.end(), [this](auto& session_ptr)
  {
    if(!session_ptr.expired())
    {
      return;
    }

    std::shared_ptr<mfsync::filetransfer::session_base> session = nullptr;
    if(ctx_.has_value())
    {
      session =
          std::make_shared<mfsync::filetransfer::client_tls_session>(io_context_,
                                                                     ctx_.value(),
                                                                     request_queue_,
                                                                     file_handler_);
    }
    else
    {
      session =
          std::make_shared<mfsync::filetransfer::client_session>(io_context_,
                                                                 request_queue_,
                                                                 file_handler_);
    }

    session_ptr = session;
    session->set_progress(progress_);
    session->start_request();
  });
}

void file_receive_handler::add_to_request_queue(available_file file)
{
  const auto equal_shasum = [&file](const auto& request_file)
     { return file.file_info.sha256sum == request_file.file_info.sha256sum; };

  if(request_queue_.contains(equal_shasum))
  {
    return;
  }

  if(file_handler_.in_progress(file))
  {
    return;
  }

  spdlog::debug("adding file to request queue: {}", file.file_info.file_name);
  request_queue_.push_back(std::move(file));
}

void file_receive_handler::wait()
{
  timer_.expires_from_now(boost::posix_time::milliseconds(50));
  timer_.async_wait(
      boost::bind(&file_receive_handler::handle_timeout, this,
        boost::asio::placeholders::error));
}

void file_receive_handler::handle_timeout(const boost::system::error_code& error)
{
  if(!error)
  {
    get_files();
  }
  else
  {
    spdlog::error("error while waiting for new files: {}", error.message());
  }
}

} //closing namespace mfsync
