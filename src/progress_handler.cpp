#include "mfsync/progress_handler.h"

namespace mfsync::filetransfer
{

progress_handler::progress_handler()
{
  show_console_cursor(false);
  bars_.set_option(option::HideBarWhenComplete{false});

  worker_thread_ = std::thread{[this]()
  {
    while(running_)
    {
      for(int i = 0; i < percentages_.size(); ++i)
      {
        if(percentages_[i] == 100)
        {
          if(!bars_[i].is_completed())
          {
            bars_[i].set_progress(percentages_[i]);
            bars_[i].mark_as_completed();
          }

          continue;
        }

        bars_[i].set_progress(percentages_[i]);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }};
}

progress_handler::~progress_handler()
{
  show_console_cursor(true);
}

std::size_t progress_handler::create_bar(const std::string& filename)
{
  std::unique_lock lk{mutex_};

  auto bar = std::make_shared<ProgressBar>(option::BarWidth{50},
                                           option::ForegroundColor{Color::red},
                                           //option::Start{"\r["},
                                           option::ShowPercentage{true},
                                           option::PrefixText{filename});
  others_.emplace_back(bar);

  auto index = bars_.push_back(*others_.back().get());
  percentages_.push_back(0);
  return index;
}

ProgressBar& progress_handler::get(size_t index)
{
  return bars_[index];
}

void progress_handler::set_progress(size_t index, int percentage)
{
  //bars_[index].set_progress(percentage);
  percentages_[index] = percentage;
}

} //closing namespace mfsync::filetransfer
