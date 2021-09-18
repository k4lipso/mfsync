#pragma once

enum class operation_mode
{
  SYNC = 0,
  SHARE,
  FETCH,
  GET,
  NONE
};

namespace misc
{

inline operation_mode get_mode(const std::string& input)
{
  const std::map<std::string, operation_mode> mode_map
  {
    { "sync", operation_mode::SYNC },
    { "share", operation_mode::SHARE },
    { "fetch", operation_mode::FETCH },
    { "get", operation_mode::GET },
  };

  if(!mode_map.contains(input))
  {
    return operation_mode::NONE;
  }

  return mode_map.at(input);
}

}
