#include "mfsync/file_information.h"

#include <fstream>

#include "spdlog/spdlog.h"
#include "openssl/sha.h"

namespace mfsync
{

std::optional<file_information> file_information::create_file_information(const std::filesystem::path& path,
                                                                          const std::filesystem::path& base,
                                                                          bool calculate_shasum /* = false */)
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

  std::optional<std::string> shasum;
  if(calculate_shasum)
  {
    shasum = get_sha256sum(path);

    if(!shasum.has_value())
    {
      spdlog::debug("couldnt get sha256sum during file_information creation");
      return std::nullopt;
    }
  }

  file_information result;
  result.file_name = std::filesystem::relative(path, base).string();
  result.sha256sum = shasum;
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

bool file_information::compare_sha256sum(const file_information& file, const std::filesystem::path& path)
{
  if(!file.sha256sum.has_value())
  {
    spdlog::error("Comparing file without sha256sum. This is not possible.");
    return false;
  }

  spdlog::debug("calculate sha256sum of {}", file.file_name);
  const auto sha256sum = file_information::get_sha256sum(path);

  if(!sha256sum.has_value())
  {
    spdlog::debug("failed to get_sha256sum");
    return false;
  }

  if(sha256sum.value() != file.sha256sum)
  {
    spdlog::info("received file has different sha256sum than requested file! Aborting");
    return false;
  }

  return true;
}


} //closing namespace mfsync
