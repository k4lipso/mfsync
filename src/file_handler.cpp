#include "mfsync/file_handler.h"

#include "spdlog/spdlog.h"
#include "openssl/sha.h"
#include <fstream>

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
      spdlog::debug("sha256sum generation failed, path doesnt point to a regular file");
      return std::nullopt;
    }

    FILE *file = fopen(path.c_str(), "rb");
    if(!file) return std::nullopt;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    const int bufSize = 32768;
    unsigned char *buffer = reinterpret_cast<unsigned char*>(malloc(bufSize));
    int bytesRead = 0;
    if(!buffer) return std::nullopt;
    while((bytesRead = fread(buffer, 1, bufSize, file)))
    {
        SHA256_Update(&sha256, buffer, bytesRead);
    }
    SHA256_Final(hash, &sha256);

    std::array<char, 65> outputBuffer;

    for(int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        sprintf(outputBuffer.data() + (i * 2), "%02x", hash[i]);
    }
    outputBuffer[64] = 0;

    fclose(file);
    free(buffer);
    return std::string{outputBuffer.data(), 65};
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
