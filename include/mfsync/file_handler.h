#pragma once

#include <boost/asio.hpp>
#include <string>
#include <set>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <filesystem>
#include <fstream>

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

	class ofstream_wrapper
	{
	public:
		ofstream_wrapper(const requested_file& file)
			: requested_file_(file)
		{
		}

		~ofstream_wrapper()
		{
			if(auto shared_token = write_token_.lock())
			{
				*shared_token.get() = false;
			}

			ofstream_.close();
		}

		ofstream_wrapper(ofstream_wrapper&& other) = default;
		ofstream_wrapper& operator=(ofstream_wrapper&& other) = default;

		bool operator!() const
		{
			return !ofstream_;
		}

		void open(const std::string& filename, std::ios_base::openmode mode = std::ios_base::out)
		{
			ofstream_.open(filename.c_str(), mode);
		}

		void open(const char *filename, std::ios_base::openmode mode = std::ios_base::out)
		{
			ofstream_.open(filename, mode);
		}

		void write(const char* s, std::streamsize count, size_t offset = 0)
		{
			ofstream_.seekp(offset);
			ofstream_.write(s, count);
			ofstream_.flush();
		}

		void write(const char* chunk)
		{
			write(chunk, requested_file_.chunksize, 0);
		}

		std::ofstream::traits_type::pos_type tellp()
		{
			return ofstream_.tellp();
		}

		void flush()
		{
			ofstream_.flush();
		}

		void set_token(std::weak_ptr<std::atomic<bool>> token)
		{
			write_token_ = token;
		}

		std::ofstream ofstream_;

	private:

		mfsync::requested_file requested_file_;
		std::weak_ptr<std::atomic<bool>> write_token_;
	};

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
    void remove_available_file(const available_file& file);
    std::optional<available_file> get_available_file(const std::string& sha256sum) const;
    void add_available_file(available_file file);
    void add_available_files(available_files available);
    stored_files get_stored_files();
    available_files get_available_files() const;
    std::condition_variable& get_cv_new_available_files();

    //TODO: replace int by ofstreamwrapper
    std::optional<mfsync::ofstream_wrapper> create_file(requested_file& requested);
    bool finalize_file(const mfsync::file_information& file);
    std::optional<std::ifstream> read_file(const file_information& file_info);

  private:

    std::filesystem::path get_tmp_path(const file_information& file_info) const;
    std::filesystem::path get_storage_path(const file_information& file_info) const;
    bool update_stored_files();
    void add_stored_file(file_information file);
    bool stored_file_exists(const file_information& file) const;
    bool stored_file_exists(const std::string& sha256sum) const;
    std::filesystem::path get_path_to_stored_file(const file_information& file_info) const;

		bool is_blocked_internal(const std::string& name) const;
		bool is_blocked_internal(const file_information& file_info) const;
		bool exists_internal(const std::string& name) const;
		bool exists_internal(const file_information& file_info) const;

    std::filesystem::path storage_path_;
    std::filesystem::path tmp_path_;
    stored_files stored_files_;
    available_files available_files_;
    std::condition_variable cv_new_available_file_;
    locked_files locked_files_;

    static constexpr const char* TMP_FOLDER = ".mfsync/";

    mutable std::mutex mutex_;
  };

} //closing namespace mfsync
