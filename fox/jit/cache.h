// vim: expandtab:ts=2:sw=2

#pragma once

#include <list>
#include <map>
#include <unordered_map>
#include <mutex>
#include <cassert>
#ifdef _WIN64
#include <Windows.h>
#else
#include <sys/mman.h>
#endif

#include "fox/fox_types.h"
#include "fox/memtable.h"

namespace fox {
namespace jit {

/*!
 * @class fox::jit::CacheEntry
 * @brief A single contiguous block of memory that has entered the JIT cache.
 *        Should be extended by each specific JIT implementation to include
 *        storage for its compilation data.
 */
class CacheEntry : public reference_counted {
public:
  /* Note: End address is not inclusive - i.e. end minus start equals length
   *       in bytes. */
  CacheEntry(const u32 virt_address, const u32 phys_address, const u32 size)
    : m_virtual_address(virt_address),
      m_physical_address(phys_address),
      m_size(size),
      m_invalidated(false),
      m_compiled(false),
      m_queued_for_compile(false)
  {
    assert(size > 0);
  }

  CacheEntry(const CacheEntry &other) = delete;

  virtual ~CacheEntry()
  {
    return;
  }

  /*!
   * @brief Compile the cached block into something suitable for execution on
   *        the current host.
   */
  virtual bool compile() = 0;

  u32 virtual_address() const
  {
    return m_virtual_address;
  }

  u32 physical_address() const
  {
    return m_physical_address;
  }

  u32 size() const
  {
    return m_size;
  }

  void set_compiled()
  {
    m_compiled = true;
  }

  void set_is_queued(const bool queued)
  {
    m_queued_for_compile = queued;
  }

  void set_is_invalidated()
  {
    m_invalidated = true;
  }

  bool is_compiled() const
  {
    return m_compiled;
  }

  bool is_queued() const
  {
    return m_queued_for_compile;
  }

  bool is_invalidated() const
  {
    return m_invalidated;
  }

  /* XXX Hack heuristic to chain to the block most likely to be executed after
   *     this block (except itself). */
  ref<CacheEntry> next_block;

private:
  const u32 m_virtual_address;
  const u32 m_physical_address;
  const u32 m_size;
  std::atomic<bool> m_invalidated;
  std::atomic<bool> m_compiled;
  std::atomic<bool> m_queued_for_compile;
};

/*!
 * @class fox::jit::Cache
 * @brief Storage of collected jit routines. The routines may be compiled or
 *        waiting to be compiled. Allows efficient lookup of the routines by
 *        address and update / invalidation by watching the guest CPU's memory.
 */
class Cache final : public MemoryWatcher {
public:
  Cache(MemoryTable *guest_memory);
  ~Cache();

  /*
   * Methods that can only safely be called from one thread (typically the CPU
   * thread). Concurrent access must be protected by an external lock.
   */

  /*!
   * @brief Try to find an existing CacheEntry that starts at the given guest
   *        virtual address. If no entry is found for that address, returns
   *        nullptr. Will not invalidate entries.
   *
   * Must only be called from the CPU thread.
   */
  CacheEntry *lookup(u32 entry_address);

  /*!
   * @brief Insert a new cache entry without immediately queueing it for
   *        compilation. Automatically invalidates any existing entries that
   *        the unit overlaps.
   *
   * Must only be called from the CPU thread.
   */
  void insert(CacheEntry *unit);

  /*!
   * @brief Collect all invalidated entries and free them. All external
   *        references to cache entries are invalidated by this call and should
   *        not be used.
   *
   * Must only be called from the CPU thread.
   */
  bool garbage_collect();

  /*!
   * @brief Queue an existing unit for compilation or recompilation.
   *
   * Must only be called from the CPU thread.
   */
  void queue_compile_unit(CacheEntry *unit);

  /*
   * Methods that are thread safe.
   */

  /*!
   * @brief Find the first entry following the provided guest address and
   *        return its start location. If no entry exists, returns UINT32_MAX.
   */
  u32 trailing_unit(u32 guest_address) const;

  /*!
   * @brief Handle a memory dirty callback from the guest's MemoryTable.
   *        Will invalidate any compilation units in the address range.
   */
  void memory_dirtied(u32 address, u32 length);

  /* For now, giving direct access to the internals for the debug GUI. In the
   * future, probably makes sense to use reader/writer locks and ref counts
   * of some kind... */

  /* ... XXX ... */
  void lock()
  {
    m_invalidation_lock.lock();
  }

  /* ... XXX ... */
  void unlock()
  {
    m_invalidation_lock.unlock();
  }

  /* ... XXX ... */
  std::map<u32, ref<CacheEntry>> &data()
  {
    return m_trailing_map;
  }

  std::multimap<u32, ref<CacheEntry>> &invalidation_map()
  {
    return m_invalidation_map;
  }

  /*!
   * @brief Invalidate all cache entries.
   */
  void invalidate_all();

private:
  /*!
   * @brief Reference to the virtual memory range where the guest CPU's native
   *        instructions are stored. This is used to respond to overwrites of
   *        the source instructions and invalidate cache entries.
   */
  MemoryTable *const m_guest_memory;

  /*!
   * @brief Our handle for creating memory watches in guest memory.
   */
  const MemoryTable::WatcherHandle m_memory_handle;

  /*!
   * @brief Lock protecting access to the cache and compilation queue.
   */
  mutable std::mutex m_invalidation_lock;

  /*!
   * @brief List of CacheEntry objects that have been invalidated by a write
   *        and must be freed on the next CPU thread access.
   */
  std::list<ref<CacheEntry>> m_dirty_queue;

  /*!
   * @brief List of CacheEntry objects that have been queued for compilation.
   *        Processed by run_compilation().
   */
  std::list<ref<CacheEntry>> m_compile_queue;

  /*!
   * @brief Mapping from the virtual start address of compilation units to their
   *        handle in memory. The unit may or may not already be compiled.
   *
   * Can only be accessed by the CPU thread. Removals are queued through the
   * dirty queue.
   */
  std::unordered_map<u32, ref<CacheEntry>> m_lookup_map;

  /*!
   * @brief Mapping from the virtual start address of compilation units to their
   *        handle in memory. The unit may or may not already be compiled.
   *
   * May be accessed by multiple threads. Protected by m_invalidation_lock.
   */
  std::map<u32, ref<CacheEntry>> m_trailing_map;

  /*!
   * @brief Mapping from the end address (start address plus length) in physical
   *        memory to a cache entry. Used to lookup entries during invalidation.
   *
   * May be accessed by multiple threads. Protected by m_invalidation_lock.
   * Needs to be a multimap because the same code may be executed from different
   * virtual addresses and each receives a unique cache entry.
   */
  std::multimap<u32, ref<CacheEntry>> m_invalidation_map;

  /*!
   * @brief For each physical page (MemoryTable::PAGE_SIZE) in guest memory,
   *        tracks the number of cache entries in that address range. Used to
   *        decide when memory watches should be removed.
   *
   * Protection by m_invalidation_lock.
   */
  std::vector<u8> m_memory_map;

  /*!
   * @brief Invalidate a range of physical guest addresses in the JIT cache.
   *        Internal use only. Must be called while holding the invalidation
   *        lock.
   *
   * Range is in the form [start, end) (i.e. end not inclusive).
   */
  void needs_lock_invalidate_range(u32 start_address, u32 end_address);

  /*!
   * @brief Process the compilation queue.
   *
   * TODO Once this is run from a different thread, handle locking.
   */
  void run_compilation();
};

}
}
