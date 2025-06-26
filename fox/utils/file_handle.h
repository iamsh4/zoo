#pragma once

#ifdef _WIN64
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace fox {

/*!
 * @class fox::FileHandle
 * @brief Wrapper around an OS native file handle that adds RAII.
 */
class FileHandle {
#ifdef _WIN64
  static constexpr HANDLE InvalidHandle = NULL;
  static constexpr auto Close = ::CloseHandle;
  using Handle = HANDLE;
#else
  static constexpr int InvalidHandle = -1;
  static constexpr auto Close = ::close;
  using Handle = int;
#endif

public:
  FileHandle()
    : m_fd(InvalidHandle)
  {
    return;
  }

  FileHandle(const Handle fd)
    : m_fd(fd)
  {
    return;
  }

  FileHandle(FileHandle &&other)
    : m_fd(other.m_fd)
  {
    other.m_fd = InvalidHandle;
  }

  FileHandle &operator=(FileHandle &&other)
  {
    if (this == &other) {
      return *this;
    }

    if (m_fd != InvalidHandle) {
      Close(m_fd);
    }

    m_fd = other.m_fd;
    other.m_fd = InvalidHandle;

    return *this;
  }

  FileHandle(const FileHandle &other) = delete;
  FileHandle &operator=(const FileHandle &other) = delete;

  ~FileHandle()
  {
    Close(m_fd);
  }

  Handle native() const
  {
    return m_fd;
  }

  bool valid() const
  {
    return m_fd >= 0;
  }

  bool operator!() const
  {
    return m_fd == InvalidHandle;
  }

private:
  Handle m_fd;
};

}
