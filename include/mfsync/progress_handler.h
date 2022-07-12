#pragma once

#include <memory>

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/cursor_control.hpp>

namespace mfsync::filetransfer
{

using namespace indicators;

class progress_handler
{
public:
  progress_handler();
  ~progress_handler();

  std::size_t create_bar(const std::string& filename);
  ProgressBar& get(size_t index);
  void set_progress(size_t index, int percentage);

  template<typename Option>
  void set_option(size_t index, Option option)
  {
    bars_[index].set_option(option);
  }

private:
  DynamicProgress<ProgressBar> bars_;
  std::vector<int> percentages_;
  std::vector<std::shared_ptr<ProgressBar>> others_;
  std::mutex mutex_;
  std::thread worker_thread_;
  bool running_ = true;
};

} //closing namespace mfsync::filetransfer
