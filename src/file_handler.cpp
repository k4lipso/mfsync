#include "mfsync/file_handler.h"

#include <fstream>
#include <cstdio>

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

    return std::string{output_buffer.data(), 65};
  }

  file_handler::file_handler(std::string storage_path)
    : storage_path_{std::move(storage_path)}
  {}

  bool file_handler::can_be_stored(const file_information& file_info) const
  {
    static_cast<void>(file_info);
    //TODO: check if enough space for storing
    return true;
  }

  void file_handler::add_available_file(available_file file)
  {
    std::unique_lock lk{mutex_};
    available_files_.insert(std::move(file));
    lk.unlock();
    cv_new_available_file_.notify_all();
  }

  std::set<file_information, std::less<>> file_handler::get_stored_files() const
  {
    std::scoped_lock lk{mutex_};
    return stored_files_;
  }

  std::condition_variable& file_handler::get_cv_new_available_files()
  {
    return cv_new_available_file_;
  }

  bool file_handler::update_stored_files()
  {
    if(storage_path_.empty())
    {
      spdlog::error("cant update storage, no storage path was given");
      return false;
    }

		spdlog::debug("updating storage. target directory: {}", storage_path_);
		for(const auto &entry : std::filesystem::directory_iterator(storage_path_))
		{
			const std::string name = entry.path().filename().string();

			spdlog::debug("adding {}", name);

			auto file_info = file_information::create_file_information(entry.path());

			if(!file_info.has_value())
			{
				spdlog::debug("file_information creation of file '{}' failed during update stroge.", entry.path().c_str());
				continue;
			}

			add_stored_file(std::move(file_info.value()));
		}

    spdlog::debug("done updating storage");
    return true;
  }

  void file_handler::add_stored_file(file_information file)
  {
    stored_files_.insert(std::move(file));
  }

  bool file_handler::stored_file_exists(const file_information& file) const
  {
    return stored_files_.contains(file);
  }

  bool file_handler::stored_file_exists(const std::string& file) const
  {
    return stored_files_.contains(file);
  }
}
