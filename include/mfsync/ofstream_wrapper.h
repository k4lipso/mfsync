#pragma once

#include <fstream>

#include "mfsync/file_information.h"

namespace mfsync
{

class ofstream_wrapper
{
public:
  ofstream_wrapper() = default;
  ofstream_wrapper(const requested_file& file);
 ~ofstream_wrapper();

  ofstream_wrapper(ofstream_wrapper &&other) = default;
  ofstream_wrapper(const ofstream_wrapper& other) = delete;
  ofstream_wrapper& operator=(ofstream_wrapper&& other) = default;
  ofstream_wrapper& operator=(const ofstream_wrapper& other) = delete;

  bool operator!() const;
  void open(const std::string& filename, std::ios_base::openmode mode = std::ios_base::out);
  void open(const char *filename, std::ios_base::openmode mode = std::ios_base::out);
  void write(const char* s, std::streamsize count, size_t offset = 0);
  void write(const char* chunk);
  std::ofstream::traits_type::pos_type tellp();
  void flush();
  void set_token(std::weak_ptr<std::atomic<bool>> token);

  std::ofstream ofstream_;

private:

  mfsync::requested_file requested_file_;
  std::weak_ptr<std::atomic<bool>> write_token_;
};

} //closing namespace mfsync
