#pragma once

#include <string>
#include <set>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <filesystem>

#include "mfsync/ofstream_wrapper.h"
#include "mfsync/file_information.h"

namespace mfsync
{

  class file_handler
  {
  public:
    using stored_files = std::set<file_information, std::less<>>;
    using available_files = std::set<available_file, std::less<>>;
    using locked_files = std::vector<std::pair<file_information, std::shared_ptr<std::atomic<bool>>>>;
    file_handler() = default;
    ~file_handler() = default;

    file_handler(std::string storage_path);

    void init_storage(std::string storage_path);
    bool can_be_stored(const file_information& file_info) const;
    bool is_available(const std::string& sha256sum) const;
    bool is_stored(const file_information& file_info) const;
    bool is_stored(const std::string& sha256sum) const;
    void remove_available_file(const available_file& file);
    std::optional<available_file> get_available_file(const std::string& sha256sum) const;
    void add_available_file(available_file file);
    void add_available_files(available_files available);
    stored_files get_stored_files();
    available_files get_available_files();
    std::condition_variable& get_cv_new_available_files();

    std::optional<mfsync::ofstream_wrapper> create_file(requested_file& requested);
    bool finalize_file(const mfsync::file_information& file);
    std::optional<std::ifstream> read_file(const file_information& file_info);

    void print_availables(bool value);

  private:

    std::filesystem::path get_tmp_path(const file_information& file_info) const;
    std::filesystem::path get_storage_path(const file_information& file_info) const;
    bool update_stored_files();
    void update_available_files();
    void add_stored_file(file_information file);
    bool stored_file_exists(const file_information& file) const;
    bool stored_file_exists(const std::string& sha256sum) const;
    std::filesystem::path get_path_to_stored_file(const file_information& file_info) const;

    bool is_blocked_internal(const std::string& name) const;
    bool is_blocked_internal(const file_information& file_info) const;
    bool exists_internal(const std::string& name) const;
    bool exists_internal(const file_information& file_info) const;

    std::filesystem::path storage_path_;
    stored_files stored_files_;
    available_files available_files_;
    std::condition_variable cv_new_available_file_;
    locked_files locked_files_;

    bool tmp_folder_initialized_ = false;
    bool print_availables_ = false;
    static constexpr const char* TMP_SUFFIX = ".mfsync";

    mutable std::mutex mutex_;
  };

} //closing namespace mfsync
