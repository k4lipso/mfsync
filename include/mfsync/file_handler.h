#pragma once

#include <boost/asio.hpp>
#include <string>
#include <set>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <filesystem>

namespace mfsync
{
  struct file_information
  {
    file_information() =default;

    static std::optional<file_information> create_file_information(const std::filesystem::path& path);
    static std::optional<std::string> get_sha256sum(const std::filesystem::path& path);

    bool operator==(const file_information& rhs) const
    {
      return file_name == rhs.file_name
          && sha256sum == rhs.sha256sum
          && size == rhs.size;
    }

    std::string file_name;
    std::string sha256sum;
    size_t size;
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

  struct available_file
  {
    file_information file_info;
    boost::asio::ip::address source_address;
    unsigned short source_port;
  };

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


  class file_handler
  {
  public:
    using stored_files = std::set<file_information, std::less<>>;
    using available_files = std::set<available_file, std::less<>>;
    file_handler() = default;
    ~file_handler() = default;

    file_handler(std::string storage_path);

    void init_storage(std::string storage_path);
    bool can_be_stored(const file_information& file_info) const;
    bool is_available(const std::string& sha256sum) const;
    void remove_available_file(const available_file& file);
    std::optional<available_file> get_available_file(const std::string& sha256sum) const;
    void add_available_file(available_file file);
    void add_available_files(available_files available);
    stored_files get_stored_files();
    available_files get_available_files() const;
    std::condition_variable& get_cv_new_available_files();

  private:

    bool update_stored_files();
    void add_stored_file(file_information file);
    bool stored_file_exists(const file_information& file) const;
    bool stored_file_exists(const std::string& sha256sum) const;
    std::filesystem::path get_path_to_stored_file(const file_information& file_info) const;

    std::filesystem::path storage_path_;
    stored_files stored_files_;
    std::set<file_information> locked_files_;
    available_files available_files_;
    std::condition_variable cv_new_available_file_;

    mutable std::mutex mutex_;
  };

} //closing namespace mfsync
