#include "mfsync/progress_handler.h"

namespace mfsync::filetransfer
{

progress_handler::progress_handler()
{
  show_console_cursor(false);
  bars_.set_option(option::HideBarWhenComplete{false});
  start();
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

} //closing namespace mfsync::filetransfer
