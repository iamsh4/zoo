#pragma once

#include <fstream>
#include <fmt/core.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unistd.h>
#include <vector>
#include <stdexcept>

#include "shared/error.h"
#include "shared/types.h"

namespace serialization {

class Stream {
public:
  Stream(size_t initial_capacity = 1)
    : _storage(initial_capacity),
      _write_index(0),
      _read_index(0)
  {
  }

  const u8 *data() const
  {
    return _storage.data();
  }

  size_t size() const
  {
    return _write_index;
  }

  template<typename... T>
  void write(T &&...args)
  {
    (write_one(args), ...);
  }

  void write_raw(const void *ptr, size_t num_bytes)
  {
    const size_t needed_size = _write_index + num_bytes;
    if (_storage.size() < needed_size)
      _storage.resize(needed_size);

    memcpy(&_storage[_write_index], ptr, num_bytes);
    _write_index += num_bytes;
  }

  void write_raw_from_ifstream(std::ifstream& in, const size_t num_bytes)
  {
    const size_t needed_size = _write_index + num_bytes;
    if (_storage.size() < needed_size)
      _storage.resize(needed_size);

    in.read(reinterpret_cast<char *>(&_storage[_write_index]), num_bytes);
    const size_t bytes_read = in.gcount();
    _check(num_bytes == bytes_read, "Failed to read all bytes from ifstream.");
    
    _write_index += num_bytes;
  }

  template<typename... T>
  void read(T &...args)
  {
    (read_one(args), ...);
  }

  void read_raw(void *ptr, size_t num_bytes)
  {
    memcpy(ptr, &_storage[_read_index], num_bytes);
    _read_index += num_bytes;
  }

private:
  /***********************************************************************/
  /* Write Methods */

  /** Write basic integer/floating-point types to stream. */
  template<typename T>
  typename std::enable_if<std::is_arithmetic<T>::value, void>::type write_one(
    const T &value)
  {
    write_raw(&value, sizeof(T));
  }

  /** Write std::array of basic integer/floating-point types to stream. */
  template<typename T, size_t array_count>
  typename std::enable_if<std::is_arithmetic<T>::value, void>::type write_one(
    const std::array<T, array_count> &array)
  {
    write_raw(array.data(), sizeof(T) * array_count);
  }

  /** Write std::vector of basic integer/floating-point types to stream. */
  template<typename T>
  typename std::enable_if<std::is_arithmetic<T>::value, void>::type write_one(
    const std::vector<T> &vec)
  {
    const u32 vec_elements = vec.size();
    write_one(vec_elements);
    write_raw(vec.data(), sizeof(T) * vec_elements);
  }

  /***********************************************************************/
  /* Read Methods */

  /** Read basic integer/floating-point types from stream. */
  template<typename T>
  typename std::enable_if<std::is_arithmetic<T>::value, void>::type read_one(
    T &value)
  {
    read_raw(&value, sizeof(T));
  }

  /** Read std::array of basic integer/floating-point types from stream. */
  template<typename T, size_t array_count>
  typename std::enable_if<std::is_arithmetic<T>::value, void>::type read_one(
    std::array<T, array_count> &array)
  {
    read_raw(array.data(), sizeof(T) * array_count);
  }

  /** Read std::vector of basic integer/floating-point types from stream. */
  template<typename T>
  typename std::enable_if<std::is_arithmetic<T>::value, void>::type read_one(
    std::vector<T> &vec)
  {
    u32 vec_elements;
    read_one(vec_elements);

    vec.resize(vec_elements);
    read_raw(vec.data(), sizeof(T) * vec_elements);
  }

  std::vector<u8> _storage;
  size_t _write_index;
  size_t _read_index;
};

}