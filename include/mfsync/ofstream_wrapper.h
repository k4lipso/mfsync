#pragma once

#include <fstream>

#include "mfsync/file_information.h"

namespace mfsync
{

class ofstream_wrapper
{
public:
  ofstream_wrapper() = default;
  ofstream_wrapper(const requested_file& file)
    : requested_file_(file)
  {
  }

  ofstream_wrapper(ofstream_wrapper &&other) = default;
  ofstream_wrapper(const ofstream_wrapper& other) = delete;

  ofstream_wrapper& operator=(ofstream_wrapper&& other) = default;
  ofstream_wrapper& operator=(const ofstream_wrapper& other) = delete;

  ~ofstream_wrapper()
  {
    if(auto shared_token = write_token_.lock())
    {
      *shared_token.get() = false;
    }

    ofstream_.close();
  }

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

} //closing namespace mfsync
