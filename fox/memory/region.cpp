// vim: expandtab:ts=2:sw=2

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <cstring>
#include <errno.h>

#include "fox/fox_utils.h"
#include "utils/error.h"
#include "memory/region.h"

namespace fox {

/*!
 * @brief Helper function to open a host file for MemoryRegion's constructor
 *        and check that it is large enough for the requested mapping offset and
 *        length.
 */
static int
check_and_open_file(const std::string &path, const size_t minimum_size)
{
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    throw std::system_error(errcode(), "open");
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    close(fd);
    throw std::system_error(errcode(), "fstat");
  }

  if (st.st_size < 0 || size_t(st.st_size) < minimum_size) {
    close(fd);
    throw std::runtime_error("Backing file not large enough");
  }

  return fd;
}

/*!
 * @brief Helper function to create and open a memory-backed file handle.
 */
static int
check_and_open_memory(const std::string &name, const size_t size)
{
#if defined(ZOO_OS_LINUX)
  const int fd = shm_open(name.c_str(), O_RDWR, S_IRWXU);
#elif defined(ZOO_OS_MACOS)
  const int fd = shm_open(name.c_str(), O_RDWR);
#else
#error "Unsupported OS"
#endif

  if (fd < 0) {
    throw std::system_error(errcode(), "shm_open");
  }

  if (ftruncate(fd, size) != 0) {
    throw std::system_error(errcode(), "ftruncate");
  }

  return fd;
}

std::string
create_unique_shm_name(const std::string &name)
{
  char buffer[512];
  const int pid = getpid();
  snprintf(buffer, sizeof(buffer), "pid-%u-shm-%s", pid, name.c_str());
  return buffer;
}

MemoryRegion::MemoryRegion(file_tag_t,
                           const std::string &name,
                           const std::string &path,
                           const size_t offset,
                           const size_t size)
  : m_type(Type::HostFile),
    m_name(create_unique_shm_name(name)),
    m_fd(check_and_open_file(path, offset + size)),
    m_device(nullptr),
    m_size(size),
    m_file_offset(offset)
{
  return;
}

MemoryRegion::MemoryRegion(memory_tag_t, const std::string &name, const size_t size)
  : m_type(Type::Memory),
    m_name(create_unique_shm_name(name)),
    m_fd(check_and_open_memory(name, size)),
    m_device(nullptr),
    m_size(size),
    m_file_offset(0lu)
{
  return;
}

MemoryRegion::MemoryRegion(mmio_tag_t,
                           const std::string &name,
                           MMIODevice *const device,
                           const size_t size)
  : m_type(Type::Device),
    m_name(name),
    m_device(nullptr),
    m_size(size),
    m_file_offset(0lu)
{
  return;
}

MemoryRegion::~MemoryRegion()
{
  if (m_fd.valid()) {
    if (shm_unlink(m_name.c_str()) != 0) {
      fprintf(stderr,
              "Failed to unlink shared memory '%s': %s\n",
              m_name.c_str(),
              strerror(errno));
      exit(1);
    }
  }
}

}
