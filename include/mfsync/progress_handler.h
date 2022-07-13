#pragma once

#include <memory>
#include <algorithm>

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/cursor_control.hpp>

#include <spdlog/spdlog.h>

#include "mfsync/file_information.h"

namespace mfsync::filetransfer
{

using namespace indicators;

namespace progress
{
  enum class STATUS
  {
    UNKNOWN,
    UPLOADING,
    DOWNLOADING,
    COMPARING,
    DONE,
    INITIALIZING //when shasum needs to be calculated on lazy evaluated storage
  };

  using bar_ptr = std::unique_ptr<ProgressBar>;
  struct file_progress_information : public file_information
  {
    file_progress_information(const file_information& info)
      : file_information{info}
    {}

    bool update_bar()
    {
      if(bar == nullptr)
      {
        return false;
      }

      bool result = update_status();
      return result || update_progress();
    }

    std::atomic<size_t> bytes_transferred = 0;
    std::atomic<STATUS> status = STATUS::UNKNOWN;
    std::atomic<bool> done = false;
    bar_ptr bar = nullptr;

  private:

    bool update_status()
    {
      if(status == old_status)
      {
        return false;
      }

      old_status.store(status.load());

      std::string message;
      auto color = indicators::Color::unspecified;
      switch(old_status)
      {
        case STATUS::UPLOADING:
          {
            color = Color::green;
            message = "uploading: ";
            break;
          }
        case STATUS::DOWNLOADING:
          {
            color = Color::red;
            message = "downloading: ";
            break;
          }
        case STATUS::COMPARING:
          {
            color = Color::blue;
            message = "comparing: ";
            break;
          }
        case STATUS::DONE:
          {
            color = Color::yellow;
            message = "done: ";
            break;
          }
        case STATUS::INITIALIZING:
          {
            color = Color::cyan;
            message = "initializing: ";
            break;
          }
        case STATUS::UNKNOWN:
          {
            color = Color::white;
            message = "unknown: ";
            break;
          }
      }

      auto hash_name = sha256sum;
      hash_name.resize(8);
      bar->set_option(option::PrefixText(message + hash_name));
      bar->set_option(option::ForegroundColor(color));

      return true;
    }

    bool update_progress()
    {
      if(bytes_transferred == old_bytes_transferred)
      {
        return false;
      }

      old_bytes_transferred.store(bytes_transferred.load());
      const size_t percentage = (static_cast<double>(old_bytes_transferred) / size) * 100;
      bar->set_progress(percentage);

      if(percentage == 100 && !bar->is_completed())
      {
        bar->mark_as_completed();
      }

      return true;
    }

    std::atomic<STATUS> old_status = STATUS::UNKNOWN;
    std::atomic<size_t> old_bytes_transferred = 0;
  };

}

class progress_handler
{
public:
  progress_handler();
  ~progress_handler();

  using file_progress_ptr = progress::file_progress_information*;
  using progress_vector = std::vector<std::unique_ptr<progress::file_progress_information>>;

  void start();
  void stop();

  file_progress_ptr create_file_progress(const file_information& file_info)
  {
    const auto compare_func = [&file_info](const auto& info)
    {
      return file_info.file_name == info->file_name
          && file_info.sha256sum == info->sha256sum;
    };

    std::unique_lock lk{mutex_};
    const auto find_result = std::find_if(files_.begin(), files_.end(), compare_func);

    if(find_result != files_.end())
    {
      return find_result->get();
    }

    auto file_progress = std::make_unique<progress::file_progress_information>(file_info);
    file_progress->bar = create_bar();

    auto* result = file_progress.get();
    files_.push_back(std::move(file_progress));
    return result;
  }

private:

  progress::bar_ptr create_bar()
  {
    auto bar = std::make_unique<ProgressBar>(option::BarWidth{50},
                                             option::ForegroundColor{Color::red},
                                             option::ShowPercentage{true}
                                             /*option::PrefixText{filename}*/);
    bars_.push_back(*bar.get());
    return bar;
  }

  std::mutex mutex_;
  progress_vector files_;

  DynamicProgress<ProgressBar> bars_;
  std::thread worker_thread_;
  bool running_ = true;
};


} //closing namespace mfsync::filetransfer
