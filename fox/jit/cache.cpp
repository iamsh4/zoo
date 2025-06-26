// vim: expandtab:ts=2:sw=2

#include "fox/jit/cache.h"

namespace fox {
namespace jit {

Cache::Cache(MemoryTable *const guest_memory)
  : m_guest_memory(guest_memory),
    m_memory_handle(guest_memory->add_watcher(this)),
    m_memory_map(guest_memory->physical_address_limit() / MemoryTable::PAGE_SIZE, 0u)
{
  return;
}

Cache::~Cache()
{
  m_compile_queue.clear();
}

u32
Cache::trailing_unit(const u32 guest_address) const
{
  std::unique_lock<std::mutex> guard(m_invalidation_lock);
  const auto execute_it = m_trailing_map.upper_bound(guest_address);
  if (execute_it == m_trailing_map.end()) {
    return UINT32_MAX;
  }

  return execute_it->second->virtual_address();
}

CacheEntry *
Cache::lookup(const u32 entry_address)
{
  const auto execute_it = m_lookup_map.find(entry_address);
  if (execute_it == m_lookup_map.end()) {
    return nullptr;
  }

  return execute_it->second.get();
}

void
Cache::insert(CacheEntry *const unit)
{
  {
    /* Invalidation lock must be held to insert unit into the invalidation
     * lookup map. */
    std::unique_lock<std::mutex> guard(m_invalidation_lock);

    /* Invalidate any overlapping cache entries. */
    const u32 phys_start = unit->physical_address();
    const u32 phys_end   = phys_start + unit->size();
    needs_lock_invalidate_range(phys_start, phys_end);

    /* Add entry to lookup maps. */
    m_lookup_map.emplace(unit->virtual_address(), unit);
    m_trailing_map.emplace(unit->virtual_address(), unit);
    m_invalidation_map.emplace(phys_end, unit);

    const u32 from_page        = phys_start / MemoryTable::PAGE_SIZE;
    const u32 first_page_after = (phys_end / MemoryTable::PAGE_SIZE) +
                                 ((phys_end & MemoryTable::PAGE_MASK) == 0 ? 0u : 1u);
    for (u32 i = from_page; i < first_page_after; ++i) {
      assert(i < m_memory_map.size());
      if (m_memory_map[i]++ == 0u) {
        m_guest_memory->add_watch(m_memory_handle, i, 1);
      }
    }
  }
}

void
Cache::invalidate_all()
{
  std::unique_lock<std::mutex> guard(m_invalidation_lock);
  needs_lock_invalidate_range(0, 0xffffffffu);
}

bool
Cache::garbage_collect()
{
  /* Perform an unsafe check whether there are any blocks to free. If there
   * are, actually take the lock and free them. */
  if (m_dirty_queue.empty()) {
    return false;
  }

  std::lock_guard _(m_invalidation_lock);
  for (ref<CacheEntry> &entry : m_dirty_queue) {
    m_lookup_map.erase(entry->virtual_address());
    m_trailing_map.erase(entry->virtual_address());
  }
  m_dirty_queue.clear();
  return true;
}

void
Cache::queue_compile_unit(CacheEntry *const unit)
{
  /* Ensure the unit was not invalidated in the moment before queueing it for
   * compilation. */
  const auto it = m_lookup_map.find(unit->virtual_address());
  assert(it->second.get() == unit);
  (void)it;

  if (unit->is_queued()) {
    /* Already in the compilation queue. */
    return;
  }

  unit->set_is_queued(true);
  m_compile_queue.emplace_back(ref<CacheEntry>(unit));

  /* TODO Queue compilation for running on a background thread */
  if (true) {
    run_compilation();
  }
}

void
Cache::memory_dirtied(const u32 start_address, const u32 length)
{
  std::unique_lock<std::mutex> guard(m_invalidation_lock);
  needs_lock_invalidate_range(start_address, start_address + length);
}

void
Cache::needs_lock_invalidate_range(const u32 start_address, const u32 end_address)
{
  auto it = m_invalidation_map.upper_bound(start_address);
  if (it == m_invalidation_map.end()) {
    return;
  }

  while (it != m_invalidation_map.end()) {
    assert(!it->second->deleted);
    if (it->second->physical_address() >= end_address) {
      break;
    }

    /* Atomically mark the block as invalidated, so the CPU thread will not
     * attempt to execute it again. */
    ref<CacheEntry> unit(it->second.get());
    unit->set_is_invalidated();

    /* Remove from compile queue */
    if (unit->is_queued()) {
      /* TODO Implement once compilation can run in background. */
    }

    /* Remove memory watches for the invalidated unit, if there are no more
     * units remaining in those pages. */
    const u32 phys_start       = unit->physical_address();
    const u32 phys_end         = phys_start + unit->size();
    const u32 from_page        = phys_start / MemoryTable::PAGE_SIZE;
    const u32 first_page_after = (phys_end / MemoryTable::PAGE_SIZE) +
                                 ((phys_end & MemoryTable::PAGE_MASK) == 0 ? 0u : 1u);
    for (u32 i = from_page; i < first_page_after; ++i) {
      assert(m_memory_map[i] > 0u);
      if (--m_memory_map[i] == 0u) {
        m_guest_memory->remove_watch(m_memory_handle, i, 1);
      }
    }

    it = m_invalidation_map.erase(it);
    m_dirty_queue.emplace_back(unit);
  }
}

void
Cache::run_compilation()
{
  while (!m_compile_queue.empty()) {
    ref<CacheEntry> unit = m_compile_queue.front();
    const bool compiled  = unit->compile();
    if (!compiled) {
      /* XXX Failed to compile... now what? */
      m_compile_queue.pop_front();
      continue;
    }

    unit->set_is_queued(false);
    unit->set_compiled();
    m_compile_queue.pop_front();
  }
}

}
}
