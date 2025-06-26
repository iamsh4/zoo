// vim: expandtab:ts=2:sw=2

#pragma once

#include <utility>
#include <atomic>
#include <cstdint>

#include "fox/fox_types.h"
#include "fox/fox_utils.h"
#include "fox/jit/routine.h"

namespace fox {
namespace codegen {

/*!
 * @brief fox::codegen::RoutineStorage
 * @brief Internal class used to allocate large chunks of executable memory
 *        and share it among multiple routines. This reduces the overhead of
 *        mmap() calls.
 *
 * Memory is initially mapped without PROT_EXEC. Pages that have been filled
 * completely (or zero-filled by a forced flush) will be remapped on-demand to
 * be executable.
 */
class RoutineStorage {
public:
  static const size_t MAP_GRANULARITY;
  static const size_t MMAP_SIZE = 256lu * 1024lu;
  static const size_t ALLOC_ALIGNMENT = 32lu;

  static_assert(is_power2(ALLOC_ALIGNMENT));

public:
  RoutineStorage();
  ~RoutineStorage();

  /*!
   * @brief Allocate the requested number of bytes from executable memory.
   *        This automatically increments the internal reference count by one
   *        unless the allocation fails.
   */
  u8 *allocate(const size_t bytes);

  /*!
   * @brief Callback to indicate a routine has been freed. This reduces the
   *        internal reference count by one, and destroys the storage object
   *        when it reaches 0.
   */
  void freed();

  /*!
   * @brief Attempt to enable execute permissions up to the indicated byte
   *        in the storage block. Returns true if the remap was completed
   *        (executable_bytes() will return a value at least the passed value).
   *
   * If this would waste memory by requiring the active page to be partially
   * wasted, the remap is only done if force is true.
   */
  bool executable_remap(const size_t target, const bool force);

  /*!
   * @brief Return the offset in bytes of the provided pointer from the start
   *        of the storage buffer. The pointer must be to data in the storage
   *        buffer.
   */
  size_t offset_of(const u8 *const data) const
  {
    return size_t(data) - size_t(m_memory);
  }

  /*!
   * @brief Returns the number of bytes (from the start of the storage block)
   *        that have been remapped as executable.
   */
  size_t executable_bytes() const
  {
    return m_memory_executable;
  }

private:
  /* Initial implict reference owned by caller of 'new'. */
  std::atomic<u32> m_references { 1u };
  u8 *m_memory = nullptr;
  size_t m_memory_size = 0lu;
  size_t m_memory_allocated = 0lu;
  size_t m_memory_executable = 0lu;
};

/*!
 * @class fox::codegen::Routine
 * @brief Container for host-executable code. Handles allocation of executable
 *        memory regions.
 */
class Routine : public jit::Routine {
public:
  Routine();
  Routine(const u8 *data, uint32_t data_size);
  virtual ~Routine();

  Routine(const Routine &other) = delete;
  Routine &operator=(const Routine &other) = delete;

  /*!
   * @brief Return a pointer to the start of the stored executable code.
   */
  const void *data() const
  {
    return m_storage.second;
  }

  /*!
   * @brief Returns the size in bytes of the executable code.
   */
  size_t size() const
  {
    return m_data_size;
  }

  /*!
   * @brief Returns true if execute() can be called on this instance. If false,
   *        a successful call to prepare() must be done first.
   */
  bool ready() const
  {
    return m_storage.first->executable_bytes() >= m_data_end;
  }

  /*!
   * @brief Attempt to prepare this routine for execution (i.e. by remapping
   *        memory with the appropriate permissions). If force is not true,
   *        it may fail and return false.
   */
  bool prepare(bool force);

  /* Implementation of jit::Routine virtual API */
  uint64_t execute(Guest *guest,
                   void *memory_base = nullptr,
                   void *register_base = nullptr) override;

  virtual void debug_print();

protected:
  const std::pair<RoutineStorage *, u8 *> m_storage;
  const uint32_t m_data_size;
  const uint32_t m_data_end;

  /*!
   * @brief ...
   */
  static std::pair<RoutineStorage *, u8 *> create_buffer(const u8 *data,
                                                         size_t data_size);
};

}
}
