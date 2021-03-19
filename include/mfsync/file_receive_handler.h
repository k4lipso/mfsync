#pragma once

#include <deque>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "spdlog/spdlog.h"

#include "mfsync/file_handler.h"

namespace mfsync
{

class file_receive_handler
{
public:
  file_receive_handler(boost::asio::io_service& service, mfsync::file_handler* file_handler)
    : timer_(service)
    , file_handler_(file_handler)
    , request_all_{true}
  {}

  file_receive_handler(boost::asio::io_service& service, mfsync::file_handler* file_handler,
                       std::vector<std::string> files_to_request)
    : timer_(service)
    , file_handler_(file_handler)
    , files_to_request_(std::move(files_to_request))
    , request_all_{false}
  {}

  void set_files(std::vector<std::string> files_to_request)
  {
    files_to_request_ = std::move(files_to_request);
  }

  void request_all_files()
  {
    files_to_request_.clear();
    request_all_ = true;
  }

  void get_files()
  {
    std::scoped_lock lk{mutex_};
    auto availables = file_handler_->get_available_files();

    if(request_all_)
    {
      for(auto& available : availables)
      {
        request_file(std::move(available));
      }

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

    wait_for_new_files();
  }

private:

  void request_file(available_file file)
  {
    if(std::any_of(request_queue_.begin(), request_queue_.end(),
       [&file](const auto& request_file)
       { return file.file_info.sha256sum == request_file.file_info.sha256sum; }))
    {
      return;
    }

    spdlog::info("adding file to request queue: {}", file.file_info.file_name);
    request_queue_.push_back(std::move(file));
  }

  void wait_for_new_files()
  {
    timer_.expires_from_now(boost::posix_time::seconds(1));
    timer_.async_wait(
        boost::bind(&file_receive_handler::handle_timeout, this,
          boost::asio::placeholders::error));
  }

  void handle_timeout(const boost::system::error_code& error)
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

  boost::asio::deadline_timer timer_;
  mfsync::file_handler* file_handler_;
  std::vector<std::string> files_to_request_;
  std::deque<available_file> request_queue_;
  bool request_all_;
  mutable std::mutex mutex_;
};

} //closing namespace mfsync
