// vim: expandtab:ts=2:sw=2

#pragma once

#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map> /* XXX */
#include <vector>

#include "fox/utils/file_handle.h"
#include "fox/fox_types.h"

namespace fox {

class MMIODevice;

/*!
 * @class MemoryWatcher
 * @brief Interface used by modules that want to know when specific memory
 *        regions have been written to.
 */
class MemoryWatcher {
public:
  virtual ~MemoryWatcher();
  virtual void memory_dirtied(u32 address, u32 length) = 0;
};

/*!
 * @class MemoryRegion
 * @brief Wrapper class that represents a region of readable and/or
 *        writable memory in a MemoryTable.
 *
 * If mapped from a local (host) file, fd must be valid.
 * Otherwise data is considered general purpose RAM and initialized to 0.
 */
class MemoryRegion {
public:
  /*!
   * @brief Access type for the memory region
   */
  enum AccessType
  {
    ReadOnly,
    SDRAM,
    MMIO
  };

  /* The type of access used for the memory region */
  const AccessType type;

  /* Device physical memory location */
  const uint32_t phys_offset;
  const uint32_t phys_end;

  /* Source file information */
  const FileHandle fd;
  const size_t file_offset;

  /* MMIO callback information */
  MMIODevice *const mmio;

  /* Human-readable name for the region */
  const std::string name;

  MemoryRegion(AccessType type, uint32_t phys_offset, uint32_t length, const char *name)
    : type(type),
      phys_offset(phys_offset),
      phys_end(phys_offset + (length - 1u)),
      fd(),
      file_offset(0lu),
      mmio(nullptr),
      name(name)
  {
    return;
  }

  MemoryRegion(AccessType type,
               uint32_t phys_offset,
               uint32_t length,
               FileHandle &&fd,
               size_t file_offset,
               const char *name)
    : type(type),
      phys_offset(phys_offset),
      phys_end(phys_offset + (length - 1u)),
      fd(std::move(fd)),
      file_offset(file_offset),
      mmio(nullptr),
      name(name)
  {
    return;
  }

  MemoryRegion(AccessType type,
               uint32_t phys_offset,
               uint32_t length,
               MMIODevice *mmio,
               const char *name)
    : type(type),
      phys_offset(phys_offset),
      phys_end(phys_offset + (length - 1u)),
      fd(),
      file_offset(0lu),
      mmio(mmio),
      name(name)
  {
    return;
  }

  ~MemoryRegion()
  {
    return;
  }

  bool operator<(const MemoryRegion &other) const
  {
    return phys_offset < other.phys_offset;
  }
};

class MemoryAccessStatistics {
public:
  class AddressCounter {
    std::mutex m_count_mutex;
    std::unordered_map<u32, u64> m_counts;

  public:
    void reset()
    {
      std::lock_guard<std::mutex> guard(m_count_mutex);
      m_counts.clear();
    }
    void increment(u32 address, u64 amount = 1)
    {
      std::lock_guard<std::mutex> guard(m_count_mutex);
      m_counts[address] += amount;
    }
    std::unordered_map<u32, u64> counts()
    {
      std::lock_guard<std::mutex> guard(m_count_mutex);
      return m_counts;
    }
  };

  struct {
    AddressCounter mmio_reads;
    AddressCounter mmio_writes;
  } counters;
};

/*!
 * @class MemoryTable
 * @brief Table of physical memory regions on a simulated device
 */
class MemoryTable {
public:
  static constexpr u32 PAGE_SIZE = 128u;
  static constexpr u32 PAGE_MASK = PAGE_SIZE - 1u;
  using WatcherHandle = uint8_t;

  MemoryAccessStatistics m_access_stats; /* XXX */

  MemoryTable(u64 max_virtual_address, u64 max_physical_address);
  virtual ~MemoryTable();

  /*!
   * @brief Return the size of the physical address range for this memory
   *        table.
   */
  u64 physical_address_limit() const
  {
    return m_physical_max;
  }

  /*!
   * @brief Map standard SDRAM into the device memory.
   */
  void map_sdram(uint32_t address, uint32_t length, const char *name);

  /*!
   * @brief Map share-capable SDRAM into the device memory. The returned
   *        MemoryRegion pointer can be used to re-map the same memory at
   *        a second location, but is otherwise identical to map_sdram().
   */
  const MemoryRegion *map_shared(uint32_t address, uint32_t length, const char *name);

  /*!
   * @brief Map an existing share-capable memory region into a second
   *        location. Reads and writes to this location will be as if the
   *        same reads and writes were done to the original region.
   */
  void map_shared(uint32_t address,
                  uint32_t length,
                  const char *name,
                  const MemoryRegion *parent,
                  uint32_t parent_offset);

  /*!
   * @brief Map a file from the host filesystem into the device memory
   */
  void map_file(uint32_t address,
                uint32_t length,
                const char *filename,
                size_t file_offset);

  /*!
   * @brief Map an MMIO region controlled via callback implementor
   */
  void map_mmio(uint32_t address, uint32_t length, const char *name, MMIODevice *device);

  /*!
   * @brief Calculate internal tables based on previously added maps. Must
   *        be called once after all mappings have been created.
   */
  void finalize();

  /*!
   * @brief Register a new callback to respond to memory modification. Memory
   *        regions the caller is interested in must be added to the watch list
   *        separately with add_watch.
   */
  WatcherHandle add_watcher(MemoryWatcher *watch);

  /*!
   * @brief Start watching a region of memory for modifications. The specified
   *        consumer will have its callback executed (in the modifier's thread)
   *        on write.
   *
   * The watch location must be aligned to PAGE_SIZE, and length must be in
   * units of PAGE_SIZE. Watches do not stack - adding a watch for an address
   * twice is an error.
   */
  void add_watch(WatcherHandle consumer, u32 start_page, u32 count);

  /*!
   * @brief Stop watching a region of memory for modifications.
   *
   * The watch location must be aligned to PAGE_SIZE, and length must be in
   * units of PAGE_SIZE. Watches do not stack - removing a watch for an address
   * twice is an error.
   */
  void remove_watch(WatcherHandle consumer, u32 start_page, u32 count);

  /*!
   * @brief Retrieve the host-visible address for a particular device address
   */
  template<typename T>
  void write(uint32_t offset, T value);

  /*!
   * @brief Read a value of size u8, u16, u32, or u64 from the mapped physical
   *        range. May trigger MMIO logic.
   */
  template<typename T>
  T read(uint32_t offset);

  /*!
   * @brief Perform a block read from the MemTable, like a DMA transfer would,
   *        and causing an error if any source portion is MMIO or unmapped.
   */
  void dma_read(void *dest, u32 source, uint32_t length);

  /*!
   * @brief Perform a block write to the MemTable, like a DMA transfer would,
   *        and causing an error if any destination portion is MMIO or unmapped.
   */
  void dma_write(u32 offset, const void *source, uint32_t length);

  /*!
   * @brief Check if memory in the given range is normal RAM (i.e., no
   *        MMIO callbacks and is threadsafe). This doesn't give permission to
   *        directly use memcpy() outside debugging contexts - there may still
   *        be watchers that need to be notified on write.
   */
  bool check_ram(u32 offset, u32 length) const;

  /*!
   * @brief Check if memory in the given range is safe to read directly (RAM or
   *        ROM mapping). This doesn't give permission to directly use memcpy()
   *        outside debugging contexts.
   */
  bool check_rom(u32 offset, u32 length) const;

  /*!
   * @brief Return a low-level access pointer to the in-memory mapping of the
   *        table. Not all addresses in the range will be valid. Writes are
   *        not allowed through this interface.
   */
  const u8 *root() const;

  /*!
   * @brief Return a low-level access list of the regions present in the
   *        physical mapping. Modifications are not allowed.
   */
  std::vector<const MemoryRegion *> regions() const;

  /*!
   * @brief Get a list of pages that are dirty.
   */
  std::vector<u32> get_dirty_pages() const;

  void dump_u32(const char *output_file_path, u32 address, u32 length);

private:
  /*!
   * @brief Maximum virtual address mapped by the memory table
   */
  const u64 m_address_max;

  /*!
   * @brief Maximum physical address mapped by the memory table
   */
  const u64 m_physical_max;

  /*!
   * @brief Number of PAGE_SIZE pages in the physical address range
   */
  const u32 m_page_count;

  /*!
   * @brief Pointer to the in-host mapped memory region
   *
   * Can be used for direct access to ReadOnly file maps or SDRAM regions,
   * but MMIO will not work directly.
   */
  u8 *const root_mem;

  using region_map = std::map<u32, std::unique_ptr<MemoryRegion>>;

  /*!
   * @brief Correspondence of region ending offset to its metadata
   */
  region_map m_regions;

  /*!
   * @brief Map of a PAGE_SIZE granularity that tracks whether a region of
   *        memory is RAM-like and/or has callbacks registered to watch for
   *        modifications.
   *
   * The 2 LSBs track whether the region is RAM-like or ROM-like. The LSB bit
   * tracks readability and the next bit tracks writability. A 0 value is true
   * in this case (so a sweep with '|' gives the right answer). i.e. 3u means
   * it's probably MMIO, and 1u means it's ROM. The remaining 6 bits map to
   * memory watch callbacks in the m_watchers array.
   * TODO:  The above doc needs updating.
   */
  std::vector<u8> m_watch_map;

  /*!
   * @brief Set of instances that are watching for memory write events on
   *        some memory range.
   */
  std::vector<MemoryWatcher *> m_watchers;

  /*!
   * @brief Count the number of MemoryRegion instances that overlap with
   *        the provided range.
   */
  size_t count_regions(u32 phys_start, u32 phys_end);

  /*!
   * @brief Find the MemoryRegion instance that maps a certain address offset.
   *        Throws exception if region does not exist.
   */
  MemoryRegion *lookup_region(u32 phys_start);

  /*!
   * @brief Helper method to allocate a range of virtual addresses without
   *        creating a physical mapping.
   */
  static void *vmap_reserve(u64 size);

  /*!
   * @brief Helper method to map a file as a read-only region of virtual
   *        memory.
   */
  FileHandle vmap_file(const std::string &path,
                       u32 file_offset,
                       u32 mem_offset,
                       u32 length);

  void execute_watcher_cbs(u8 mask, u32 address, u32 length);

  void clear_dirty_bits();

public:
  /**
   * @brief An iterator over MemoryRegion* in an underlying MemoryTable
   */
  class RegionIterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = MemoryRegion *;
    using pointer = value_type *;
    using reference = value_type &;

    RegionIterator(region_map::iterator underlying) : m_underlying(underlying), m_current(m_underlying->second.get()) {}

    const value_type &operator*() const
    {
      return m_current;
    }

    pointer operator->()
    {
      return &m_current;
    }

    // Pre-increment
    RegionIterator &operator++()
    {
      m_underlying++;
      m_current = m_underlying->second.get();
      return *this;
    }

    // Post-increment
    RegionIterator operator++(int)
    {
      RegionIterator tmp = *this;
      ++(*this);
      return tmp;
    }

    bool operator==(const RegionIterator &other)
    {
      return m_underlying == other.m_underlying;
    }
    bool operator!=(const RegionIterator &other)
    {
      return m_underlying != other.m_underlying;
    }

  private:
    region_map::iterator m_underlying;
    value_type m_current;
  };

  /** @brief Convenient wrapper for accessing regions in range-based-for */
  class MemoryRegions {
  public:
    RegionIterator begin()
    {
      return RegionIterator { m_memory_table.m_regions.begin() };
    }

    RegionIterator end()
    {
      return RegionIterator { m_memory_table.m_regions.end() };
    }

  private:
    MemoryRegions(MemoryTable &table) : m_memory_table(table) {}

    MemoryTable &m_memory_table;

    friend class ::fox::MemoryTable;
  };

  /** @brief Gives access to begin/end iterators over all MemoryRegions */
  MemoryRegions regions()
  {
    return MemoryRegions(*this);
  }
};

}
