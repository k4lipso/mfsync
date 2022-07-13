#pragma once

#include <memory>
#include <algorithm>

#include <indicators/dynamic_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/cursor_control.hpp>

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

    bool update_bar();

    std::atomic<size_t> bytes_transferred = 0;
    std::atomic<STATUS> status = STATUS::UNKNOWN;
    std::atomic<bool> done = false;
    bar_ptr bar = nullptr;

  private:

    bool update_status();
    bool update_progress();

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

  file_progress_ptr create_file_progress(const file_information& file_info);

private:

  progress::bar_ptr create_bar();

  std::mutex mutex_;
  progress_vector files_;
  DynamicProgress<ProgressBar> bars_;
  std::thread worker_thread_;
  bool running_ = true;
};


} //closing namespace mfsync::filetransfer
