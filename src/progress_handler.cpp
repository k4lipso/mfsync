#include "mfsync/progress_handler.h"

#include <spdlog/spdlog.h>

namespace mfsync::filetransfer
{

namespace progress
{

  bool file_progress_information::update_bar()
  {
    if(bar == nullptr)
    {
      return false;
    }

    bool result = update_status();
    return result || update_progress();
  }

  bool file_progress_information::update_status()
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
          color = Color::white;
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
    bar->set_option(option::PostfixText(message + file_name));
    bar->set_option(option::ForegroundColor(color));

    return true;
  }

  bool file_progress_information::update_progress()
  {
    if(bytes_transferred == old_bytes_transferred)
    {
      return false;
    }

    old_bytes_transferred.store(bytes_transferred.load());
    const size_t percentage = (static_cast<double>(old_bytes_transferred) / size) * 100;
    bar->set_progress(percentage);

    return true;
  }
}

progress_handler::progress_handler()
{
  show_console_cursor(true);
  bars_.set_option(option::HideBarWhenComplete{false});
}

progress_handler::~progress_handler()
{
  show_console_cursor(true);
}

void progress_handler::start()
{
  const auto progress_loop = [this]() {
    while(running_)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::unique_lock lk{mutex_};

      bool has_changed = false;
      for(const auto& file_progress : files_)
      {
        has_changed |= file_progress->update_bar();
      }

      if(has_changed)
      {
        bars_.print_progress();
      }
    }
  };

  worker_thread_ = std::thread{progress_loop};
}

void progress_handler::stop()
{
  running_ = false;
}

progress_handler::file_progress_ptr progress_handler::create_file_progress(const file_information& file_info)
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

progress::bar_ptr progress_handler::create_bar()
{
  auto bar = std::make_unique<ProgressBar>(option::BarWidth{0},
                                           option::Start{""},
                                           option::End{""},
                                           option::ForegroundColor{Color::red},
                                           option::PrefixText(""),
                                           option::PostfixText(""),
                                           option::ShowPercentage{true});
  bars_.push_back(*bar.get());
  return bar;
}


} //closing namespace mfsync::filetransfer
