#pragma once

#include <filesystem>
#include <optional>
#include <utility>

#include <boost/asio.hpp>
#include <nlohmann/json.hpp>

namespace mfsync
{
  struct file_information
  {
    static std::optional<file_information> create_file_information(const std::filesystem::path& path,
                                                                   const std::filesystem::path& base,
                                                                   bool calculate_shasum = false);
    static std::optional<std::string> get_sha256sum(const std::filesystem::path& path);
    static bool compare_sha256sum(const file_information& file, const std::filesystem::path& path);

    bool operator==(const file_information& rhs) const
    {
      return file_name == rhs.file_name
          //&& sha256sum == rhs.sha256sum
          && size == rhs.size;
    }

    std::string file_name;
    std::optional<std::string> sha256sum = std::nullopt;
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

  struct host_information
  {
    std::string public_key;
    std::string version;
    std::string ip;
    unsigned short port;
  };

  inline void from_json(const nlohmann::json& j, host_information& host_info) {
    j.at("public_key").get_to(host_info.public_key);
    j.at("port").get_to(host_info.port);
    j.at("version").get_to(host_info.version);
  }


  inline void to_json(nlohmann::json& j, const file_information& file_info) {
    j = nlohmann::json{{"file_name", file_info.file_name},
             {"size", file_info.size}};

    if(file_info.sha256sum.has_value())
    {
      j["sha256sum"] = file_info.sha256sum.value();
    }
  }

  inline void from_json(const nlohmann::json& j, file_information& file_info) {
    j.at("file_name").get_to(file_info.file_name);
    j.at("size").get_to(file_info.size);

    if(j.contains("sha256sum"))
    {
      j.at("sha256sum").get_to(file_info.sha256sum);
    }
  }

  inline void to_json(nlohmann::json& j, const requested_file& requested) {
    j = nlohmann::json{{"offset", requested.offset},
             {"chunksize", requested.chunksize}};

    j["file_info"] = requested.file_info;
  }

  inline void from_json(const nlohmann::json& j, requested_file& requested) {
    j.at("offset").get_to(requested.offset);
    j.at("chunksize").get_to(requested.chunksize);
    requested.file_info = j.at("file_info").get<file_information>();
  }

  inline void from_json(const nlohmann::json& j, available_file& available) {
    j.at("port").get_to(available.source_port);
    available.file_info = j.get<file_information>();
  }

  inline bool operator==(const requested_file& lhs, const requested_file& rhs)
  {
    return lhs.file_info == rhs.file_info
        && lhs.chunksize == rhs.chunksize
        && lhs.offset == rhs.offset;
  }

  inline bool operator<(const file_information& file_info, const std::string& file_name)
  {
    return file_info.file_name < file_name;
  }

  inline bool operator<(const std::string& file_name, const file_information& file_info)
  {
    return file_name < file_info.file_name;
  }

  inline bool operator<(const file_information& lhs, const file_information& rhs)
  {
    return lhs.file_name < rhs.file_name;
  }

  inline bool operator<(const available_file& available, const std::string& file_name)
  {
    return available.file_info < file_name;
  }

  inline bool operator<(const std::string& file_name, const available_file& available)
  {
    return file_name < available.file_info;
  }

  inline bool operator<(const available_file& lhs, const available_file& rhs)
  {
    return lhs.file_info < rhs.file_info;
  }
} //closing namespace mfsync
