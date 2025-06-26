// vim: expandtab:ts=2:sw=2

#pragma once

#include <string>

#include "utils/file_handle.h"

namespace fox {

class MMIODevice;

/*!
 * @class MemoryRegion
 * @brief Backing storage for readable and/or writable guest memory. This can
 *        be mapped into MemoryTable instaces representing guest memory address
 *        space.
 *
 * If mapped from a local (host) file, fd must be valid.
 * Otherwise data is considered general purpose RAM and initialized to 0.
 */
class MemoryRegion {
public:
  /*!
   * @brief Implementation type for a MemoryRegion
   */
  enum class Type
  {
    HostFile,
    Memory,
    Device
  };

  /*! @brief Constructor tag for HostFile-type regions */
  struct file_tag_t {} file_type;

  /*!
   * @brief Construct a new MemoryRegion backed by a file. The mapping is always
   *        read-only.
   */
  MemoryRegion(file_tag_t, const std::string &name, const std::string &path,
               const size_t offset, const size_t size);

  /*! @brief Constructor tag for Memory-type regions */
  struct memory_tag_t {} memory_type;

  /*!
   * @brief Construct a new MemoryRegion backed by normal host memory.
   */
  MemoryRegion(memory_tag_t, const std::string &name, const size_t size);

  /*! @brief Constructor tag for MMIO-type regions */
  struct mmio_tag_t {} mmio_type;

  /*!
   * @brief Construct a new MemoryRegion backed by a class implementing the
   *        MMIODevice virtual interface.
   */
  MemoryRegion(mmio_tag_t, const std::string &name, MMIODevice *const device,
               const size_t size);

  ~MemoryRegion();

  Type type() const
  {
    return m_type;
  }

  size_t size() const
  {
    return m_size;
  }

private:
  /*!
   * @brief Implementation that provides this virtual memory region.
   */
  const Type m_type;

  /*!
   * @brief Name of this memory region for debugging and/or serialization
   *        purposes. Not used internally.
   */
  const std::string m_name;

  /*!
   * @brief File handle associated with this virtual memory region's storage.
   *        This is unused for MMIO devices.
   */
  const FileHandle m_fd;

  /*!
   * @brief MMIODevice that backs this virtual memory region. This is only used
   *        for Device-type regions and is nullptr in other cases.
   */
  MMIODevice *const m_device;

  /*!
   * @brief The size of this memory region in bytes.
   */
  const size_t m_size;

  /*!
   * @brief For HostFile type regions only. The offset in bytes of the host
   *        file data to be used for this region.
   */
  const size_t m_file_offset;
};

}
