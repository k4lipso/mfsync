#include "mfsync/file_handler.h"

#include <cstdio>

#include "boost/lexical_cast.hpp"

#include "spdlog/spdlog.h"

namespace mfsync
{
  void file_handler::init_storage(std::string storage_path)
  {
    if(!storage_path_.empty())
    {
      spdlog::debug("calling init_storage on already initialized storage_path_");
    }

    storage_path_ = std::move(storage_path);

    update_stored_files(true);
    storage_initialized_ = true;
  }

  bool file_handler::can_be_stored(const file_information& file_info) const
  {
    const auto space_info = std::filesystem::space(storage_path_);

    if(space_info.available < file_info.size)
    {
      spdlog::error("Not enough space left on device.");
      return false;
    }

    return true;
  }

  bool file_handler::is_stored(const file_information& file_info) const
  {
    std::scoped_lock lk{mutex_};
    return stored_files_.contains(file_info);
  }

  bool file_handler::is_stored(const std::string& sha256sum) const
  {
    std::scoped_lock lk{mutex_};
    return std::any_of(stored_files_.begin(), stored_files_.end(), [&sha256sum](const auto& file_info)
    {
      return file_info.file_name == sha256sum;
    });
  }

  bool file_handler::is_available(const std::string& sha256sum) const
  {
    std::scoped_lock lk{mutex_};
    return available_files_.contains(sha256sum);
  }

  void file_handler::remove_available_file(const available_file& file)
  {
    std::scoped_lock lk{mutex_};
    available_files_.erase(file);
  }

  std::optional<available_file> file_handler::get_available_file(const std::string& sha256sum) const
  {
    std::scoped_lock lk{mutex_};
    const auto it = available_files_.find(sha256sum);

    if(it != available_files_.end())
    {
      return *it;
    }

    return std::nullopt;
  }

  void file_handler::add_available_file(available_file file)
  {
    std::unique_lock lk{mutex_};

    if(stored_files_.contains(file.file_info))
    {
      return;
    }

    const auto it_bool_pair = available_files_.insert(std::move(file));
    lk.unlock();

    if(std::get<1>(it_bool_pair))
    {
      cv_new_available_file_.notify_all();
    }
  }

  void file_handler::add_available_files(const available_files& available)
  {
    std::unique_lock lk{mutex_};
    bool changed = false;

    update_stored_files();

    for(const auto& avail : available)
    {
      if(stored_files_.contains(avail.file_info))
      {
        continue;
      }

      const auto it_bool_pair = available_files_.insert(std::move(avail));

      if(std::get<1>(it_bool_pair) && print_availables_)
      {
        const auto& file_info = (*std::get<0>(it_bool_pair)).file_info;
        spdlog::info("{}", file_info.file_name);
        changed = true;
      }
    }

    lk.unlock();

    if(changed)
    {
      cv_new_available_file_.notify_all();
    }
  }

  std::set<file_information, std::less<>> file_handler::get_stored_files()
  {
    std::scoped_lock lk{mutex_};
    //update_stored_files();
    return stored_files_;
  }

  file_handler::available_files file_handler::get_available_files()
  {
    std::scoped_lock lk{mutex_};
    //update_available_files();
    return available_files_;
  }

  std::condition_variable& file_handler::get_cv_new_available_files()
  {
    return cv_new_available_file_;
  }

  bool file_handler::in_progress(const available_file& file) const
  {
    if(exists_internal(file.file_info) || is_blocked_internal(file.file_info))
    {
      return true;
    }

    return false;
  }

  std::optional<mfsync::ofstream_wrapper> file_handler::create_file(requested_file& requested)
  {
    std::scoped_lock lk{mutex_};
    if(exists_internal(requested.file_info) || is_blocked_internal(requested.file_info))
    {
      spdlog::debug("Tried creating existing or locked file");
      return std::nullopt;
    }

    if(!can_be_stored(requested.file_info))
    {
      spdlog::debug("file {} cant be stored. aborting file creation", requested.file_info.file_name);
      return std::nullopt;
    }

    if(std::filesystem::exists(get_storage_path(requested.file_info)))
    {
      spdlog::debug("Tried creating existing file that was not initialized yet");
      return std::nullopt;
    }

    const auto tmp_path = get_tmp_path(requested.file_info);
    const auto file_exists = std::filesystem::exists(tmp_path);

    if(file_exists)
    {
      requested.offset = std::filesystem::file_size(tmp_path);
      spdlog::debug("setting offset to: {}", requested.offset);
    }

    auto tmp_path_without_filename = tmp_path;
    tmp_path_without_filename.remove_filename();
    if(!std::filesystem::exists(tmp_path_without_filename))
    {
      if(!std::filesystem::create_directories(tmp_path_without_filename))
      {
        spdlog::error("Could not create directory {}", tmp_path_without_filename.string());
        return std::nullopt;
      }
    }

    ofstream_wrapper output(requested);

    if(file_exists)
    {
      output.open(tmp_path.c_str(), std::ios::in|std::ios::out|std::ios::binary);
    }
    else
    {
      output.open(tmp_path.c_str(), std::ios::out|std::ios::binary);
    }

    if(!output)
    {
      spdlog::error("failed to create file");
      return std::nullopt;
    }

    output.get_ofstream().seekp(requested.offset, output.get_ofstream().beg);

    auto token = std::make_shared<std::atomic<bool>>(true);
    output.set_token(token);
    locked_files_.emplace_back(requested.file_info, std::move(token));

    return output;
  }

  bool file_handler::finalize_file(const mfsync::file_information& file)
  {
    std::scoped_lock lk{mutex_};

    if(exists_internal(file))
    {
      spdlog::debug("tried finalizing file that already exists");
      return false;
    }

    if(!is_blocked_internal(file))
    {
      spdlog::debug("tried finalizing file that was not blocked");
      return false;
    }

    const auto tmp_path = get_tmp_path(file);


    if(finalize_with_shasum)
    {
      if(!file_information::compare_sha256sum(file, tmp_path))
      {
        return false;
      }

      spdlog::debug("shasum256 of {} is correct.", file.file_name);
    }

    spdlog::debug("adding file to storage: {}", file.file_name);
    locked_files_.erase(std::remove_if(locked_files_.begin(), locked_files_.end(),
                        [&file](const auto& locked_file){ return file == locked_file.first; }),
                        locked_files_.end());

    std::filesystem::rename(tmp_path, get_storage_path(file));
    add_stored_file(file, false);
    update_stored_files();
    return true;
  }

  std::optional<std::ifstream> file_handler::read_file(const file_information& file_info)
  {
    std::scoped_lock lk{mutex_};

    update_stored_files();
    if(!exists_internal(file_info))
    {
      spdlog::debug("Tried reading nonexisting file");
      return std::nullopt;
    }

    std::ifstream Input;
    Input.open(get_storage_path(file_info), std::ios_base::binary | std::ios_base::ate);

    if(!Input)
    {
      spdlog::error("Failed to read file");
      spdlog::error("{}",get_storage_path(file_info).c_str());
      return std::nullopt;
    }

    return Input;
  }

  void file_handler::print_availables(bool value)
  {
      print_availables_ = value;
  }

  std::filesystem::path file_handler::get_tmp_path(const file_information& file_info) const
  {
    auto tmp_path = storage_path_;
    tmp_path /= std::string{file_info.file_name + TMP_SUFFIX}.c_str();
    return tmp_path;
  }

  std::filesystem::path file_handler::get_storage_path(const file_information& file_info) const
  {
    auto tmp_path = storage_path_;
    tmp_path /= file_info.file_name.c_str();
    return tmp_path;
  }

  bool file_handler::update_stored_files(bool init_call /* = false */)
  {
    if(!storage_initialized_ && !init_call)
    {
      return false;
    }

    bool expected = false;
    if(!storage_init_is_in_progress_.compare_exchange_strong(expected, true))
    {
      return false;
    }

    if(storage_path_.empty())
    {
      return false;
    }

    if(!std::filesystem::exists(storage_path_))
    {
      spdlog::error("storage path doesnt exist");
      return false;
    }

    std::erase_if(stored_files_, [this](const auto& file_info)
      { return !std::filesystem::exists(get_path_to_stored_file(file_info));});

    update_stored_files(storage_path_);

    if(bar_ != nullptr)
    {
      bar_->status = filetransfer::progress::STATUS::INITIALIZED;
      bar_ = nullptr;
    }
    storage_init_is_in_progress_ = false;

    return true;
  }

  void file_handler::update_stored_files(const std::filesystem::path& path)
  {
    if(!storage_initialized_ && progress_ != nullptr)
    {
      if(bar_ == nullptr)
      {
        using std::filesystem::directory_iterator;
        using fp = bool (*)( const std::filesystem::path&);

        const auto is_reqular_and_no_tmp_file = [this](const std::filesystem::path& path)
        {
          return std::filesystem::is_regular_file(path) && !is_tmp_file(path);
        };

        const auto amount_files = std::count_if(std::filesystem::recursive_directory_iterator(path),
                                                std::filesystem::recursive_directory_iterator{},
                                                is_reqular_and_no_tmp_file);

        auto dummy_file_info = file_information();
        dummy_file_info.file_name = "storage";
        dummy_file_info.size = amount_files;

        bar_ = progress_->create_file_progress(dummy_file_info);
        bar_->status = filetransfer::progress::STATUS::INITIALIZING;
      }
    }

    for(const auto &entry : std::filesystem::directory_iterator(path))
    {
      if(std::filesystem::is_symlink(entry))
      {
        continue;
      }

      if(std::filesystem::is_directory(entry))
      {
        update_stored_files(entry);
        continue;
      }

      if(is_tmp_file(entry))
      {
        continue;
      }

      const std::string name = std::filesystem::relative(entry.path(), storage_path_).string();

      if(stored_files_.contains(name))
      {
        continue;
      }

      auto file_info = file_information::create_file_information(entry.path(), storage_path_);

      if(!file_info.has_value())
      {
        spdlog::debug("file_information creation of file '{}' failed during update stroge.", entry.path().c_str());
        continue;
      }

      add_stored_file(std::move(file_info.value()));

      if(bar_ != nullptr)
      {
        bar_->bytes_transferred++;
      }
    }
  }

  bool file_handler::is_tmp_file(const std::filesystem::path& path) const
  {
    const auto file_name = path.string();
    return file_name.ends_with(TMP_SUFFIX);
  }

  void file_handler::update_available_files()
  {
    for(auto it = available_files_.begin(); it != available_files_.end(); )
    {
      if(stored_files_.contains(it->file_info))
      {
        it = available_files_.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void file_handler::add_stored_file(file_information file, bool block /* = false */)
  {
    std::unique_lock lk{mutex_, std::defer_lock};

    if(block)
    {
      spdlog::info("LOCKING");
      lk.lock();
    }

    const auto result = stored_files_.insert(std::move(file));

    if(std::get<1>(result))
    {
      spdlog::debug("adding file to storage: {} - size: {}", (*std::get<0>(result)).file_name,
                                                             (*std::get<0>(result)).size);
    }
  }

  bool file_handler::stored_file_exists(const file_information& file) const
  {
    std::scoped_lock lk{mutex_};
    return stored_files_.contains(file);
  }

  bool file_handler::stored_file_exists(const std::string& file) const
  {
    std::scoped_lock lk{mutex_};
    return stored_files_.contains(file);
  }

  std::filesystem::path file_handler::get_path_to_stored_file(const file_information& file_info) const
  {
    if(storage_path_.empty())
    {
      spdlog::debug("Called get_path_to_stored_file with empty storage_path_");
    }

    auto path = storage_path_;
    path /= file_info.file_name;
    return path;
  }

  bool file_handler::is_blocked_internal(const std::string& name) const
  {
    return std::any_of(locked_files_.begin(), locked_files_.end(),
                       [&name](const auto& locked_file)
                       { return locked_file.first.file_name == name && *locked_file.second.get() == true; });
  }

  bool file_handler::is_blocked_internal(const file_information& file_info) const
  {
    return std::any_of(locked_files_.begin(), locked_files_.end(),
                       [&file_info](const auto& locked_file)
                       { return locked_file.first == file_info && *locked_file.second.get() == true; });
  }

  bool file_handler::exists_internal(const std::string& name) const
  {
    return std::any_of(stored_files_.begin(), stored_files_.end(),
                       [&name](const auto& file){ return file.file_name == name; });
  }

  bool file_handler::exists_internal(const file_information& file_info) const
  {
    return stored_files_.contains(file_info);
  }

} //closing namespace mfsync
