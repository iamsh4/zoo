// vim: expandtab:ts=2:sw=2

#include <system_error>
#include <stdexcept>
#include <cstring>
#include <cassert>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef _WIN64
#include <Windows.h>
#else
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#endif

#include "utils/error.h"
#include "utils/format.h"
#include "fox/mmio_device.h"
#include "fox/memtable.h"

namespace fox {

static constexpr u8 READABLE_BIT  = 0b001;
static constexpr u8 WRITEABLE_BIT = 0b010;
static constexpr u8 DIRTY_BIT     = 0b100;

/* Instantiations of table memory accessor templates */
template u8 MemoryTable::read(u32);
template u16 MemoryTable::read(u32);
template u32 MemoryTable::read(u32);
template u64 MemoryTable::read(u32);

template void MemoryTable::write(u32, u8);
template void MemoryTable::write(u32, u16);
template void MemoryTable::write(u32, u32);
template void MemoryTable::write(u32, u64);

MemoryTable::MemoryTable(const u64 max_virtual_address, const u64 max_physical_address)
  : m_address_max(max_virtual_address),
    m_physical_max(max_physical_address),
    m_page_count((m_address_max + PAGE_SIZE - 1u) / PAGE_SIZE),
    root_mem(reinterpret_cast<u8 *>(vmap_reserve(m_address_max))),
    m_watch_map(m_page_count, 3u)
{
#if _WIN64
  VirtualFree(root_mem, 0, MEM_RELEASE);
#endif

  /* The first three watcher entries are consumed by the RO / RW / Dirty bits. */
  m_watchers.emplace_back(nullptr /* Marks readable ranges */);
  m_watchers.emplace_back(nullptr /* Marks writable ranges */);
  m_watchers.emplace_back(nullptr /* Marks dirty ranges */);
}

MemoryTable::~MemoryTable()
{
  return;
}

void
MemoryTable::map_sdram(const u32 offset, const u32 length, const char *const name)
{
  assert(u64(offset) + u64(length) <= m_address_max);
  if (count_regions(offset, offset + length) > 0) {
    throw std::runtime_error("Cannot create overlapping table mappings");
  }

#ifndef _WIN64
  void *const sdram_ptr = mmap(&root_mem[offset],
                               length,
                               PROT_READ | PROT_WRITE,
                               MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE,
                               -1,
                               0lu);
  if (sdram_ptr == MAP_FAILED) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "mmap");
  }
#else
  void *const sdram_ptr =
    VirtualAlloc(&root_mem[offset], length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (sdram_ptr == NULL) {
    throw std::system_error(std::error_code(errno, std::generic_category()),
                            "VirtualAlloc");
  }
#endif

  m_regions.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(offset + (length - 1u)),
    std::forward_as_tuple(new MemoryRegion(MemoryRegion::SDRAM, offset, length, name)));
}

const MemoryRegion *
MemoryTable::map_shared(const u32 address, const u32 length, const char *const name)
{
  assert(u64(address) + u64(length) <= m_address_max);
  if (count_regions(address, address + length) > 0) {
    throw std::runtime_error("Cannot create overlapping table mappings");
  }

  /* TODO Check for address+length sanity */
  /* TODO Check for region collisions */

#ifndef _WIN64
  /* NOTE On MacOS the max filename length for an SHM entry is 31 characters. */
  char name_buffer[128];
  snprintf(name_buffer, sizeof(name_buffer), "shm.%u.%x.%x", getpid(), address, length);

  FileHandle hdl(shm_open(name_buffer, O_RDWR | O_CREAT | O_EXCL, 0));
  if (!hdl) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "shm_open");
  }

  if (ftruncate(hdl.native(), length) < 0) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "ftruncate");
  }

  void *const map_ptr = mmap(&root_mem[address],
                             length,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_FIXED,
                             hdl.native(),
                             0lu);
  if (map_ptr == MAP_FAILED) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "mmap");
  }

  if (shm_unlink(name_buffer) < 0) {
    throw std::system_error(std::error_code(errno, std::generic_category()),
                            "shm_unlink");
  }
#else
  wchar_t wtext[256];
  mbstowcs(wtext, name, strlen(name) + 1);
  LPWSTR wide_name = wtext;

  FileHandle hdl(
    CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, length, wide_name));
  if (!hdl) {
    throw std::system_error(std::error_code(errno, std::generic_category()),
                            "CreateFileMapping");
  }
  DWORD error;

  void *const mapping =
    MapViewOfFileEx(hdl.native(), FILE_MAP_ALL_ACCESS, 0, 0, length, &root_mem[address]);

  if (mapping == NULL) {
    error = GetLastError();
    throw std::system_error(std::error_code(error, std::system_category()),
                            "MapViewOfFile");
  }
#endif

  MemoryRegion *const region_ptr =
    new MemoryRegion(MemoryRegion::SDRAM, address, length, std::move(hdl), 0u, name);

  m_regions.emplace(std::piecewise_construct,
                    std::forward_as_tuple(address + (length - 1u)),
                    std::forward_as_tuple(region_ptr));

  return region_ptr;
}

void
MemoryTable::map_shared(const u32 address,
                        const u32 length,
                        const char *const name,
                        const MemoryRegion *parent,
                        const u32 offset)
{
  assert(u64(address) + u64(length) <= m_address_max);
  if (count_regions(address, address + length) > 0) {
    throw std::runtime_error("Cannot create overlapping table mappings");
  }

  assert(parent != NULL);
  assert(!!parent->fd);

#ifndef _WIN64
  void *const map_ptr = mmap(&root_mem[address],
                             length,
                             PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_FIXED,
                             parent->fd.native(),
                             offset);
  if (map_ptr == MAP_FAILED) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "mmap");
  }
#else
  void *const mapping = MapViewOfFileEx(
    parent->fd.native(), FILE_MAP_ALL_ACCESS, 0, offset, length, &root_mem[address]);
  if (mapping == NULL) {
    throw std::system_error(std::error_code(errno, std::generic_category()),
                            "MapViewOfFile");
  }
#endif

  m_regions.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(address + (length - 1u)),
    std::forward_as_tuple(new MemoryRegion(MemoryRegion::SDRAM, address, length, name)));
}

void
MemoryTable::map_file(const u32 offset,
                      const u32 length,
                      const char *const filename,
                      const size_t file_offset)
{
  assert(u64(offset) + u64(length) <= m_address_max);
  if (count_regions(offset, offset + length) > 0) {
    throw std::runtime_error("Cannot create overlapping table mappings");
  }

  /* TODO Check for file offset/length multiples of system's PAGE_SIZE */
  /* TODO Check for page-level collisions */

  FileHandle fd(vmap_file(filename, file_offset, offset, length));
  m_regions.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(offset + (length - 1u)),
    std::forward_as_tuple(new MemoryRegion(
      MemoryRegion::ReadOnly, offset, length, std::move(fd), file_offset, filename)));
}

void
MemoryTable::map_mmio(const u32 offset,
                      const u32 length,
                      const char *const name,
                      MMIODevice *const device)
{
  assert(u64(offset) + u64(length) <= m_address_max);
  if (count_regions(offset, offset + length) > 0) {
    for (const auto &entry : m_regions) {
      const MemoryRegion &region = *entry.second;
      if (region.phys_offset < offset && region.phys_end > (offset + length)) {
        fprintf(stderr,
                "Region %s (%u:%u) overlaps with new MMIO region %s (%u:%u)\n",
                region.name.c_str(),
                region.phys_offset,
                region.phys_end,
                name,
                offset,
                offset + length);
      }
    }

    throw std::runtime_error("Cannot create overlapping table mappings");
  }

  m_regions.emplace(std::piecewise_construct,
                    std::forward_as_tuple(offset + (length - 1u)),
                    std::forward_as_tuple(new MemoryRegion(
                      MemoryRegion::MMIO, offset, length, device, name)));
}

void
MemoryTable::finalize()
{
  /* TODO Check for page-level collisions */
  for (const auto &entry : m_regions) {
    const MemoryRegion &region = *entry.second;
    if (region.type == MemoryRegion::ReadOnly) {
      for (u32 i = region.phys_offset; i < region.phys_end; i += PAGE_SIZE) {
        m_watch_map[i / PAGE_SIZE] &= ~u8(1u);
      }
    } else if (region.type == MemoryRegion::SDRAM) {
      for (u32 i = region.phys_offset; i < region.phys_end; i += PAGE_SIZE) {
        m_watch_map[i / PAGE_SIZE] &= ~u8(3u);
      }
    }
  }
}

MemoryTable::WatcherHandle
MemoryTable::add_watcher(MemoryWatcher *const watch)
{
  /* Per-page watch details are stored as a bitmap with only 8 positions,
   * but the LSB is reserved for other purposes. */
  assert(m_watchers.size() < 8);
  const WatcherHandle handle(1u << m_watchers.size());
  m_watchers.emplace_back(watch);
  return handle;
}

void
MemoryTable::add_watch(const WatcherHandle consumer,
                       const u32 start_page,
                       const u32 count)
{
  /* Ensure the consumer is not using a reserved bit. */
  assert(!(consumer & (READABLE_BIT | WRITEABLE_BIT)));
  for (u32 i = 0u; i < count; ++i) {
    const u32 page = start_page + i;
    m_watch_map[page] |= consumer;
  }
}

void
MemoryTable::remove_watch(const WatcherHandle consumer,
                          const u32 start_page,
                          const u32 count)
{
  /* Ensure the consumer is not using a reserved bit. */
  assert(!(consumer & (READABLE_BIT | WRITEABLE_BIT)));
  for (u32 i = 0u; i < count; ++i) {
    const u32 page = start_page + i;
    assert(m_watch_map[page] & consumer);
    m_watch_map[page] &= ~consumer;
  }
}

void
MemoryTable::dma_read(void *const dest, const u32 offset, const u32 length)
{
  if (FOX_PEDANTIC(size_t(offset) + length > m_address_max)) {
    throw std::out_of_range(f("DMA address offset {:X} out of range", offset));
  }

  u8 total_mask = 0u;
  /* XXX */
  for (u32 phys = offset; phys < (offset + length); phys += PAGE_SIZE) {
    total_mask |= m_watch_map[phys / PAGE_SIZE];
  }

  if ((total_mask & READABLE_BIT) == 0u) {
    memcpy(dest, &root_mem[offset], length);
    return;
  }

  MemoryRegion *const region = lookup_region(offset);
  if (size_t(offset) + length > region->phys_end) {
    throw std::out_of_range(f("DMA address range {:X}:{:X} spans multiple memory regions",
                              offset,
                              offset + length));
  }

  switch (region->type) {
    case MemoryRegion::ReadOnly:
    case MemoryRegion::SDRAM:
      memcpy(dest, &root_mem[offset], length);
      break;

    case MemoryRegion::MMIO:
      region->mmio->read_dma(offset, length, (u8 *)dest);
      break;
  }
}

void
MemoryTable::dma_write(const u32 offset, const void *const source, const u32 length)
{
  if (FOX_PEDANTIC(size_t(offset) + length > m_address_max)) {
    throw std::out_of_range(f("DMA address offset {:X} out of range", offset));
  }

  /* XXX */
  u8 total_mask = 0u;
  for (u32 phys = offset; phys < (offset + length); phys += PAGE_SIZE) {
    // OR together all the watchers for the affected pages. (Mask later)
    total_mask |= m_watch_map[phys / PAGE_SIZE];
  }

  // If this is writeable, simply do the write.
  if ((total_mask & WRITEABLE_BIT) == 0u) {
    for (u32 phys = offset; phys < (offset + length); phys += PAGE_SIZE) {
      m_watch_map[phys / PAGE_SIZE] |= DIRTY_BIT;
    }
    memcpy(&root_mem[offset], source, length);
    execute_watcher_cbs(total_mask, offset, length);
    return;
  }

  MemoryRegion *const region = lookup_region(offset);
  if (size_t(offset) + length > region->phys_end) {
    throw std::out_of_range(f("DMA address range {:X}:{:X} spans multiple memory regions",
                              offset,
                              offset + length));
  }

  switch (region->type) {
    case MemoryRegion::ReadOnly:
      throw std::out_of_range(
        f("Attempt to DMA to ROM address 0x{:X} (DMA size {:} bytes)", offset, length));
      break;

    case MemoryRegion::SDRAM:
      for (u32 phys = offset; phys < (offset + length); phys += PAGE_SIZE) {
        m_watch_map[phys / PAGE_SIZE] |= DIRTY_BIT;
      }
      memcpy(&root_mem[offset], source, length);
      break;

    case MemoryRegion::MMIO:
      region->mmio->write_dma(offset, length, (u8 *)source);
      break;
  }

  execute_watcher_cbs(total_mask, offset, length);
}

bool
MemoryTable::check_ram(const u32 offset, const u32 length) const
{
  const u32 start_page = offset / PAGE_SIZE;
  const u32 end_page   = (offset + length) / PAGE_SIZE;
  for (u32 i = start_page; i <= end_page; ++i) {
    if ((m_watch_map[i] & (READABLE_BIT | WRITEABLE_BIT)) != 0u) {
      return false;
    }
  }

  return true;
}

bool
MemoryTable::check_rom(const u32 offset, const u32 length) const
{
  const u32 start_page = offset / PAGE_SIZE;
  const u32 end_page   = (offset + length) / PAGE_SIZE;
  for (u32 i = start_page; i <= end_page; ++i) {
    if ((m_watch_map[i] & READABLE_BIT) != 0u) {
      return false;
    }
  }

  return true;
}

template<typename T>
T
MemoryTable::read(const u32 offset)
{
  T result;

  if (FOX_PEDANTIC(size_t(offset) + sizeof(T) > m_address_max)) {
    assert(false);
    throw std::out_of_range(f("Table address offset {:X} out of range", offset));
  }

  if (FOX_PEDANTIC(size_t(offset) & (sizeof(T) - 1u))) {
    assert(false);
    throw std::out_of_range(
      f("Table address offset {:X} not aligned for {:} byte access", offset, sizeof(T)));
  }

  /* Short-circuit if it maps to a RAM-like page */
  if ((m_watch_map[offset / PAGE_SIZE] & READABLE_BIT) == 0u) {
    memcpy(&result, &root_mem[offset], sizeof(T));
    return result;
  }

  MemoryRegion *const region = lookup_region(offset);
  switch (region->type) {
    case MemoryRegion::ReadOnly:
    case MemoryRegion::SDRAM:
      memcpy(&result, &root_mem[offset], sizeof(T));
      return result;

    case MemoryRegion::MMIO:
      m_access_stats.counters.mmio_reads.increment(offset);
      result = region->mmio->read<T>(offset);
      return result;
  }

  FOX_UNREACHABLE();
}

template<typename T>
void
MemoryTable::write(const u32 offset, const T value)
{
  if (size_t(offset) + sizeof(T) > m_address_max) {
    assert(false);
    throw std::out_of_range(f("Table address offset 0x{:X} out of range", offset));
  }

  if (size_t(offset) & (sizeof(T) - 1u)) {
    assert(false);
    throw std::out_of_range(f(
      "Table address offset 0x{:X} not aligned for {:} byte access", offset, sizeof(T)));
  }

  /* Short-circuit if it maps to a RAM-only page */
  const u8 page_mask = m_watch_map[offset / PAGE_SIZE];
  if ((page_mask & WRITEABLE_BIT) == 0u) {
    m_watch_map[offset / PAGE_SIZE] |= DIRTY_BIT;
    memcpy(&root_mem[offset], &value, sizeof(T));
    execute_watcher_cbs(page_mask, offset, sizeof(T));
    return;
  }

  MemoryRegion *const region = lookup_region(offset);
  switch (region->type) {
    case MemoryRegion::ReadOnly:
      break; /* TODO */
      throw std::out_of_range(
        f("Attempt to write to ROM address 0x{:X} (write size {:} bytes)",
          offset,
          sizeof(T)));
      break;

    case MemoryRegion::SDRAM:
      m_watch_map[offset / PAGE_SIZE] |= DIRTY_BIT;
      memcpy(&root_mem[offset], &value, sizeof(T));
      break;

    case MemoryRegion::MMIO:
      // m_watch_map[offset / PAGE_SIZE] |= DIRTY_BIT;
      m_access_stats.counters.mmio_writes.increment(offset);
      region->mmio->write<T>(offset, value);
      break;
  }

  execute_watcher_cbs(page_mask, offset, sizeof(T));
}

const u8 *
MemoryTable::root() const
{
  return root_mem;
}

std::vector<const MemoryRegion *>
MemoryTable::regions() const
{
  std::vector<const MemoryRegion *> result;
  for (const auto &entry : m_regions) {
    result.push_back(entry.second.get());
  }

  return result;
}

size_t
MemoryTable::count_regions(const u32 phys_start, const u32 phys_end)
{
  size_t count = 0lu;
  for (const auto &entry : m_regions) {
    const MemoryRegion &region = *entry.second;
    if (region.phys_offset < phys_end && region.phys_end > phys_start) {
      printf("Region %s (%u:%u) overlaps with new region (%u:%u)\n",
              region.name.c_str(),
              region.phys_offset,
              region.phys_end,
              phys_start,
              phys_end);
      ++count;
    }
  }

  return count;
}

MemoryRegion *
MemoryTable::lookup_region(const u32 phys)
{
  /* TODO Optimize */
  const auto region_it = m_regions.lower_bound(phys);
  if (region_it == m_regions.end() || region_it->second->phys_offset > phys) {
    __builtin_trap();
    throw std::out_of_range(f("Table address offset 0x{:X} not mapped", phys));
  }

  return region_it->second.get();
}

void *
MemoryTable::vmap_reserve(const u64 size)
{
#ifdef _WIN64
  LPVOID result = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE);
  if (result == NULL) {
    throw std::system_error(std::error_code(errno, std::system_category()),
                            "VirtualAlloc");
  }

  return result;
#else
  void *const result = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0lu);
  if (result == MAP_FAILED) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "mmap");
  }

  return result;
#endif
}

FileHandle
MemoryTable::vmap_file(const std::string &path,
                       const u32 file_offset,
                       const u32 mem_offset,
                       const u32 length)
{
#ifdef _WIN64
  const int stringLength =
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), strlen(path.c_str()), 0, 0);
  std::wstring wstr(stringLength, 0);
  MultiByteToWideChar(
    CP_ACP, 0, path.c_str(), strlen(path.c_str()), &wstr[0], stringLength);

  // TODO : I don't know how to map a file to a particular spot so... let's just copy :)
  FileHandle fp(CreateFile(
    wstr.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
  if (!fp) {
    /* TODO error checking */
  }

  void *const sdram_ptr =
    VirtualAlloc(&root_mem[mem_offset], length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (sdram_ptr == NULL) {
    throw std::system_error(std::error_code(errno, std::generic_category()),
                            "VirtualAlloc");
  }

  /* TODO error checking. TODO handle non-zero file_offset. */
  DWORD bytesRead;
  ReadFile(fp.native(), sdram_ptr, length, &bytesRead, NULL);
#else
  FileHandle fp(open(path.c_str(), O_RDONLY));
  if (!fp) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "open");
  }

  void *const file_ptr = mmap(&root_mem[mem_offset],
                              length,
                              PROT_READ,
                              MAP_FIXED | MAP_SHARED,
                              fp.native(),
                              file_offset);
  if (file_ptr == MAP_FAILED) {
    throw std::system_error(std::error_code(errno, std::generic_category()), "mmap");
  }
#endif

  return fp;
}

void
MemoryTable::execute_watcher_cbs(const u8 mask, const u32 address, const u32 length)
{
  /* Skip first two entries, which mark RO/RW memory ranges. */
  for (unsigned i = 3; i < 8; ++i) {
    if (mask & (1u << i)) {
      m_watchers[i]->memory_dirtied(address, length);
    }
  }
}

std::vector<u32>
MemoryTable::get_dirty_pages() const
{
  std::vector<u32> results;
  for (u32 page = 0; page < m_page_count; page++) {
    if (m_watch_map[page] & DIRTY_BIT) {
      u32 phys_start = page * PAGE_SIZE;
      results.push_back(phys_start);
    }
  }
  return results;
}

void
MemoryTable::dump_u32(const char *output_file_path, u32 address, u32 length)
{
  FILE *f = fopen(output_file_path, "wb");
  for (u32 i = 0; i < length; i += sizeof(u32)) {
    const u32 word = read<u32>(address + i);
    fwrite(&word, sizeof(word), 1, f);
  }
  fclose(f);
}

void
MemoryTable::clear_dirty_bits()
{
  for (u32 page = 0; page < m_page_count; page++) {
    m_watch_map[page] &= ~DIRTY_BIT;
  }
}

MemoryWatcher::~MemoryWatcher()
{
  return;
}
}
