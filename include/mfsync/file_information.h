#pragma once

#include <filesystem>
#include <optional>
#include <utility>

#include <boost/asio.hpp>

namespace mfsync
{
  struct file_information
  {
    file_information() = default;

    static std::optional<file_information> create_file_information(const std::filesystem::path& path,
                                                                   const std::filesystem::path& base);
    static std::optional<std::string> get_sha256sum(const std::filesystem::path& path);

    bool operator==(const file_information& rhs) const
    {
      return file_name == rhs.file_name
          && sha256sum == rhs.sha256sum
          && size == rhs.size;
    }

    std::string file_name;
    std::string sha256sum;
    size_t size = 0;
  };

  struct available_file
  {
    file_information file_info;
    boost::asio::ip::address source_address;
    unsigned short source_port = 0;
  };

  struct requested_file
  {
    file_information file_info;
    size_t offset = 0;
    unsigned chunksize = 0;
  };

  inline bool operator<(const file_information& file_info, const std::string& sha256sum)
  {
    return file_info.sha256sum < sha256sum;
  }

  inline bool operator<(const std::string& sha256sum, const file_information& file_info)
  {
    return sha256sum < file_info.sha256sum;
  }

  inline bool operator<(const file_information& lhs, const file_information& rhs)
  {
    return lhs.sha256sum < rhs.sha256sum;
  }

  inline bool operator<(const available_file& available, const std::string& sha256sum)
  {
    return available.file_info < sha256sum;
  }

  inline bool operator<(const std::string& sha256sum, const available_file& available)
  {
    return sha256sum < available.file_info;
  }

  inline bool operator<(const available_file& lhs, const available_file& rhs)
  {
    return lhs.file_info < rhs.file_info;
  }
} //closing namespace mfsync
