#include "mfsync/file_receive_handler.h"

#include <boost/bind.hpp>

namespace mfsync
{

file_receive_handler::file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler)
  : io_context_(context)
  , timer_(context)
  , file_handler_(file_handler)
  , request_all_{true}
{}

file_receive_handler::file_receive_handler(boost::asio::io_context& context, mfsync::file_handler& file_handler,
                     std::vector<std::string> files_to_request)
  : io_context_(context)
  , timer_(context)
  , file_handler_(file_handler)
  , files_to_request_(std::move(files_to_request))
  , request_all_{false}
{}

void file_receive_handler::set_files(std::vector<std::string> files_to_request)
{
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

  if(!session_.expired())
  {
    wait_for_new_files();
    return;
  }

  auto availables = file_handler_.get_available_files();

  if(request_all_)
  {
    for(auto& available : availables)
    {
      request_file(std::move(available));
    }

    try_start_new_session();
    wait_for_new_files();
    return;
  }

  for(auto& sha256sum : files_to_request_)
  {
    auto it = availables.find(sha256sum);

    if(it == availables.end())
    {
      continue;
    }

    request_file(*it);
    sha256sum = "DONE";
  }

  try_start_new_session();
  wait_for_new_files();
}

void file_receive_handler::try_start_new_session()
{
  //TODO: available file should be popped by session itself. just check if request_queue_ is empty, and if not create session
  //the session then takes the first available file from the queue itself.
  spdlog::debug("creating new session");
  auto session = std::make_shared<mfsync::filetransfer::client_session>(io_context_,
                                                                        request_queue_,
                                                                        file_handler_);
  session_ = session;

  session->start_request();
}

void file_receive_handler::request_file(available_file file)
{
  auto pred = [&file](const auto& request_file)
     { return file.file_info.sha256sum == request_file.file_info.sha256sum; };

  if(request_queue_.contains(pred))
  {
    return;
  }

  spdlog::info("adding file to request queue: {}", file.file_info.file_name);
  request_queue_.push_back(std::move(file));
}

void file_receive_handler::wait_for_new_files()
{
  timer_.expires_from_now(boost::posix_time::seconds(1));
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
