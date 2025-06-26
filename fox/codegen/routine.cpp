// vim: expandtab:ts=2:sw=2

#include <atomic>
#include <mutex>
#include <cstring>
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>

#include "fox/fox_types.h"
#include "fox/fox_utils.h"
#include "fox/codegen/routine.h"

namespace fox {
namespace codegen {

/******************************************************************************
 * fox::codegen::RoutineStorage
 ******************************************************************************/

const size_t RoutineStorage::MAP_GRANULARITY = sysconf(_SC_PAGESIZE);

RoutineStorage::RoutineStorage()
{
  /* Sanity check on runtime environment */
  assert(MAP_GRANULARITY % sysconf(_SC_PAGESIZE) == 0);
  assert(MMAP_SIZE % sysconf(_SC_PAGESIZE) == 0);

  /* Map all memory as non-executable initially. */
  void *const mapping =
    mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED) {
    assert(false);
  }

  m_memory = (u8 *)mapping;
  m_memory_size = MMAP_SIZE;
  m_memory_allocated = 0lu;
}

RoutineStorage::~RoutineStorage()
{
  if (m_memory != nullptr) {
    munmap(m_memory, m_memory_size);
  }
}

u8 *
RoutineStorage::allocate(const size_t bytes)
{
  if ((m_memory_allocated + bytes) > m_memory_size) {
    return nullptr;
  }

  /* Round next allocation offset up to alignment size multiple. */
  u8 *const result = &m_memory[m_memory_allocated];
  m_memory_allocated += (bytes + ALLOC_ALIGNMENT - 1) & ~(ALLOC_ALIGNMENT - 1);

  ++m_references;
  return result;
}

void
RoutineStorage::freed()
{
  if (--m_references == 0) {
    delete this;
  }
}

bool
RoutineStorage::executable_remap(const size_t target, const bool force)
{
  if (m_memory_executable >= target) {
    /* Already remapped. */
    return true;
  }

  const size_t protect_end = round_up(target, MAP_GRANULARITY);
  if (protect_end > m_memory_allocated) {
    if (!force) {
      /* Active page would be wasted. */
      return false;
    }

    m_memory_allocated = protect_end;
  }

  assert(protect_end <= m_memory_size);

  if (mprotect(m_memory, protect_end, PROT_READ | PROT_EXEC) != 0) {
    assert(false);
  }

  m_memory_executable = protect_end;
  return true;
}

/******************************************************************************
 * fox::codegen::Routine
 ******************************************************************************/

Routine::Routine()
  : m_storage(std::make_pair(nullptr, nullptr)),
    m_data_size(0u),
    m_data_end(0u)
{
  return;
}

Routine::Routine(const u8 *const data, const uint32_t data_size)
  : m_storage(create_buffer(data, data_size)),
    m_data_size(data_size),
    m_data_end(m_storage.first->offset_of(m_storage.second) + data_size)
{
  return;
}

Routine::~Routine()
{
  if (m_storage.first != nullptr) {
    m_storage.first->freed();
  }
}

bool
Routine::prepare(const bool force)
{
  return m_storage.first->executable_remap(m_data_end, force);
}

uint64_t
Routine::execute(Guest *const guest, void *memory_base, void *register_base)
{
  typedef uint64_t (*jit_function_t)(Guest *const, void *, void *);
  jit_function_t function = (jit_function_t)m_storage.second;
  return function(guest, memory_base, register_base);
}

void
Routine::debug_print()
{
  printf("Host executable Routine: %p, %u bytes\n", m_storage.second, m_data_size);
}

/* TODO Clean this up. */
std::pair<RoutineStorage *, u8 *>
Routine::create_buffer(const u8 *const data, size_t data_size)
{
  static std::mutex storage_lock;
  static RoutineStorage *current_routine_storage = nullptr;
  std::lock_guard<std::mutex> _(storage_lock);

  if (current_routine_storage != nullptr) {
    u8 *const buffer = current_routine_storage->allocate(data_size);
    if (buffer != nullptr) {
      memcpy(buffer, data, data_size);
      return std::make_pair(current_routine_storage, buffer);
    }

    /* Free implicit ref. */
    current_routine_storage->freed();
  }

  current_routine_storage = new RoutineStorage();
  u8 *const buffer = current_routine_storage->allocate(data_size);
  assert(buffer != nullptr);

  memcpy(buffer, data, data_size);
  return std::make_pair(current_routine_storage, buffer);
}

}
}
