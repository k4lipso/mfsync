#include "mfsync/file_handler.h"

#include <fstream>
#include <cstdio>

#include "boost/lexical_cast.hpp"

#include "spdlog/spdlog.h"
#include "openssl/sha.h"

namespace mfsync
{
  std::optional<file_information> file_information::create_file_information(const std::filesystem::path& path)
  {
    if(!std::filesystem::is_regular_file(path))
    {
      spdlog::debug("creating file_information failed, path doesnt point to a regular file");
      return std::nullopt;
    }

    std::ifstream f(path.c_str());
    if(!f.good())
    {
      return std::nullopt;
    }

    auto sha256sum = get_sha256sum(path);

    if(!sha256sum.has_value())
    {
      spdlog::debug("couldnt get sha256sum during file_information creation");
      return std::nullopt;
    }

    file_information result;
    result.file_name = path.filename();
    result.sha256sum = sha256sum.value();
    result.size = std::filesystem::file_size(path);

    return result;
  }

  std::optional<std::string> file_information::get_sha256sum(const std::filesystem::path& path)
  {
    if(!std::filesystem::is_regular_file(path))
    {
      spdlog::debug("sha256sum generation failed, path doesnt point to a regular file. path was: {}",
                    path.c_str());

      return std::nullopt;
    }

    std::ifstream file{path.c_str(), std::ios::binary | std::ios::in};

    if(!file)
    {
      spdlog::debug("failed to open file: {}", path.c_str());
      return std::nullopt;
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    std::array<char, 0x8012> input_buffer;
    while(file.peek() != EOF)
    {
      const auto bytes_read = file.readsome(input_buffer.data(), input_buffer.size());
      SHA256_Update(&sha256, input_buffer.data(), bytes_read);
    }

    std::array<uint8_t, SHA256_DIGEST_LENGTH> hash;
    SHA256_Final(hash.data(), &sha256);

    std::array<char, 65> output_buffer;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        std::sprintf(output_buffer.data() + (i * 2), "%02x", hash[i]);
    }
    output_buffer[64] = 0;

    return std::string{output_buffer.data(), 64};
  }

  file_handler::file_handler(std::string storage_path)
    : storage_path_{std::move(storage_path)}
  {
    update_stored_files();
  }

  void file_handler::init_storage(std::string storage_path)
  {
    if(!storage_path_.empty())
    {
      spdlog::debug("calling init_storage on already initialized storage_path_");
    }

    storage_path_ = std::move(storage_path);
    update_stored_files();
  }

  bool file_handler::can_be_stored(const file_information& file_info) const
  {
    static_cast<void>(file_info);
    //TODO: check if enough space for storing
    return true;
  }

  bool file_handler::is_stored(const file_information& file_info) const
  {
    std::scoped_lock lk{mutex_};
    return stored_files_.contains(file_info);
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
    const auto it_bool_pair = available_files_.insert(std::move(file));
    lk.unlock();

    if(std::get<1>(it_bool_pair))
    {
      cv_new_available_file_.notify_all();
    }
  }

  void file_handler::add_available_files(available_files available)
  {
    std::unique_lock lk{mutex_};
    bool changed = false;

    for(auto& avail : available)
    {
      if(stored_files_.contains(avail.file_info))
      {
        continue;
      }

      const auto it_bool_pair = available_files_.insert(std::move(avail));

      if(std::get<1>(it_bool_pair))
      {
        spdlog::info("available: \"{}\" - {} - {}", (*std::get<0>(it_bool_pair)).file_info.file_name,
                                                    (*std::get<0>(it_bool_pair)).file_info.sha256sum,
                                                    (*std::get<0>(it_bool_pair)).file_info.size);
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
    update_stored_files();
    return stored_files_;
  }

  file_handler::available_files file_handler::get_available_files() const
  {
    std::scoped_lock lk{mutex_};
    return available_files_;
  }

  std::condition_variable& file_handler::get_cv_new_available_files()
  {
    return cv_new_available_file_;
  }

  bool file_handler::init_tmp_directory()
  {
    if(tmp_folder_initialized_)
    {
      return true;
    }

    auto tmp_path = storage_path_;
    tmp_path /= TMP_FOLDER;

    if(std::filesystem::exists(tmp_path))
    {
      tmp_folder_initialized_ = true;
      return true;
    }

    spdlog::debug("creating tmp_storage directory");
    if(!std::filesystem::create_directory(tmp_path))
    {
      spdlog::error("could not create directoy in given storage path, check permissions");
      return false;
    }

    tmp_folder_initialized_ = true;
    return true;
  }

  std::optional<mfsync::ofstream_wrapper> file_handler::create_file(requested_file& requested)
  {
    if(!init_tmp_directory())
    {
      return std::nullopt;
    }

    const std::string& filename = requested.file_info.file_name;

    std::scoped_lock lk{mutex_};
    if(exists_internal(requested.file_info) || is_blocked_internal(requested.file_info))
    {
      spdlog::debug("Tried creating existing or locked file");
      return std::nullopt;
    }

    //Update FileInfo::offset, this way GetFile::create_request requests with correct offset
    //if file was partly received before
    const auto tmp_path = get_tmp_path(requested.file_info);
    const auto file_exists = std::filesystem::exists(tmp_path);

    if(file_exists)
    {
      requested.offset = std::filesystem::file_size(tmp_path);
      spdlog::debug("setting offset to: {}", requested.offset);
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

    output.ofstream_.seekp(requested.offset, output.ofstream_.beg);

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
      spdlog::debug("Tried finalizing file that already exists");
      return false;
    }

    if(!is_blocked_internal(file))
    {
      spdlog::debug("Tried finalizing file that was not blocked");
      return false;
    }

    const auto tmp_path = get_tmp_path(file);
    const auto sha256sum = file_information::get_sha256sum(tmp_path);

    if(!sha256sum.has_value())
    {
      spdlog::debug("failed to get_sha256sum");
      return false;
    }

    if(sha256sum.value() != file.sha256sum)
    {
      spdlog::warn("Received file has different sha256sum than requested file! Aborting");
      return false;
    }

    spdlog::info("finalizing file: {}", sha256sum.value());

    locked_files_.erase(std::remove_if(locked_files_.begin(), locked_files_.end(),
                        [&file](const auto& locked_file){ return file == locked_file.first; }),
                        locked_files_.end());

    std::filesystem::rename(tmp_path, get_storage_path(file));
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

  std::filesystem::path file_handler::get_tmp_path(const file_information& file_info) const
  {
    auto tmp_path = storage_path_;
    tmp_path /= TMP_FOLDER;
    tmp_path /= file_info.file_name.c_str();
    return tmp_path;
  }

  std::filesystem::path file_handler::get_storage_path(const file_information& file_info) const
  {
    auto tmp_path = storage_path_;
    tmp_path /= file_info.file_name.c_str();
    return tmp_path;
  }

  bool file_handler::update_stored_files()
  {
    if(storage_path_.empty())
    {
      spdlog::error("cant update storage, no storage path was given");
      return false;
    }

    if(!std::filesystem::exists(storage_path_))
    {
      spdlog::error("storage path doesnt exist");
      return false;
    }

    std::erase_if(stored_files_, [this](const auto& file_info)
      { return !std::filesystem::exists(get_path_to_stored_file(file_info));});

    for(const auto &entry : std::filesystem::directory_iterator(storage_path_))
    {
      const std::string name = entry.path().filename().string();

      if(name == TMP_FOLDER)
      {
        continue;
      }

      if(std::any_of(stored_files_.begin(), stored_files_.end(),
                     [&name](const auto& file_info)
                     {  return name == file_info.file_name; }))
      {
        continue;
      }

      auto file_info = file_information::create_file_information(entry.path());

      if(!file_info.has_value())
      {
        spdlog::debug("file_information creation of file '{}' failed during update stroge.", entry.path().c_str());
        continue;
      }

      add_stored_file(std::move(file_info.value()));
    }

    return true;
  }

  void file_handler::add_stored_file(file_information file)
  {
    const auto result = stored_files_.insert(std::move(file));

    if(std::get<1>(result))
    {
      spdlog::debug("adding file to storage: \"{}\" - {} - {}", (*std::get<0>(result)).file_name,
                                            (*std::get<0>(result)).sha256sum,
                                            (*std::get<0>(result)).size);
    }
    else
    {
      spdlog::debug("file already exists");
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
