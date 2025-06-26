#pragma once

#include <filesystem>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <string>

inline std::string
read_file_to_string(const char *path)
{
  std::ifstream file_stream(path);
  std::stringstream buffer;
  buffer << file_stream.rdbuf();
  return buffer.str();
}

inline size_t
get_file_size(const char *path)
{
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  return file.tellg();
}

inline void check_file_exists(std::filesystem::path path)
{
  if (!std::filesystem::exists(path)) {
    const std::string error = "File does not exist: " + path.string();
    throw std::runtime_error(error);
  }
}