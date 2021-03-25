#include "mfsync/ofstream_wrapper.h"

namespace mfsync
{

ofstream_wrapper::ofstream_wrapper(const requested_file& file)
  : requested_file_(file)
{
}

ofstream_wrapper::~ofstream_wrapper()
{
  if(auto shared_token = write_token_.lock())
  {
    *shared_token.get() = false;
  }

  ofstream_.close();
}

bool ofstream_wrapper::operator!() const
{
  return !ofstream_;
}

void ofstream_wrapper::open(const std::string& filename, std::ios_base::openmode mode /* = ios_base::out */)
{
  ofstream_.open(filename.c_str(), mode);
}

void ofstream_wrapper::open(const char *filename, std::ios_base::openmode mode /* = ios_base::out */)
{
  ofstream_.open(filename, mode);
}

void ofstream_wrapper::write(const char* s, std::streamsize count, size_t offset /* = 0 */)
{
  ofstream_.seekp(offset);
  ofstream_.write(s, count);
  ofstream_.flush();
}

void ofstream_wrapper::write(const char* chunk)
{
  write(chunk, requested_file_.chunksize, 0);
}

std::ofstream::traits_type::pos_type ofstream_wrapper::tellp()
{
  return ofstream_.tellp();
}

void ofstream_wrapper::flush()
{
  ofstream_.flush();
}

void ofstream_wrapper::set_token(std::weak_ptr<std::atomic<bool>> token)
{
  write_token_ = token;
}

} //closing namespace mfsync
