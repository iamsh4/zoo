#include <fmt/core.h>
#include <SDL2/SDL_opengl.h>

#include "shared/profiling.h"
#include "core/console.h"
#include "gpu/texture_manager.h"
#include "gpu/texture.h"
#include "gpu/vram.h"

#if 0
#define DEBUG(args...) fmt::print(args)
#else
#define DEBUG(args...)
#endif

using namespace gpu;

Log::Logger<Log::LogModule::GRAPHICS> TextureManager::log;

TextureManager::TextureManager(Console *const console)
  : m_console(console),
    m_guest_memory(console->memory()),
    m_memory_handle(m_guest_memory->add_watcher(this)),
    m_memory_map(m_guest_memory->physical_address_limit() / fox::MemoryTable::PAGE_SIZE)
{
}

TextureManager::~TextureManager() {}

void
TextureManager::invalidate_all()
{
  std::lock_guard guard(m_cache_lock);
  m_texture_key_to_tex.clear();
}

std::vector<std::pair<u32, std::shared_ptr<Texture>>>
TextureManager::get_vram_to_textures()
{
  std::lock_guard guard(m_cache_lock);

  std::vector<std::pair<u32, std::shared_ptr<Texture>>> result;
  for (auto &e : m_texture_key_to_tex) {
    result.emplace_back(e);
  }

  return result;
}

void
TextureManager::callback_pre_render()
{
}

void
TextureManager::callback_post_render()
{
  std::lock_guard guard(m_cache_lock);

  // Delete any texture handle which hasn't been used in a while.
  for (auto it = m_texture_key_to_tex.begin(); it != m_texture_key_to_tex.end();) {
    auto &texture = it->second;
    DEBUG("callback_post :: tex (0x{:08x}, uuid {}, last_used = {}, last_updated = {})\n",
          texture->dc_vram_address,
          texture->uuid,
          texture->last_used_on_frame,
          texture->last_updated_on_frame);

    // Garbage collect any textures not used for 64 frames
    if (texture->last_used_on_frame + 64 < m_console->gpu()->get_render_count()) {

      // Reduce the number of watches over the area where this used to exist.
      auto [from_page, first_page_after] =
        get_memtable_bounds(texture->dc_vram_address, texture->dc_bytes);

      for (u32 i = from_page; i < first_page_after; ++i) {
        assert(i < m_memory_map.size());
        if (--m_memory_map[i] == 0u) {
          m_console->memory()->remove_watch(m_memory_handle, i, 1);
        }
      }

      // Remove from our internal key -> handle map. The host will follow this erasure by
      // seeing this uuid isn't present in the map anymore.
      it = m_texture_key_to_tex.erase(it);

    } else {
      ++it;
    }
  }
}

std::shared_ptr<Texture>
TextureManager::get_texture_handle(TextureKey key)
{
  const u32 vram_address = key.tex_word.address << 3; // Offset from 0x0400'0000
  unsigned width         = 8 << key.tsp_word.size_u;
  unsigned height        = 8 << key.tsp_word.size_v;

  // The purpose of this function is to return a handle to a texture object that matches
  // the input parameters. It may or may not have any color data already calculated. It
  // also tracks when the last frame this area of memory was updated.
  //
  // This texture handle will be internally reaped if no frame uses it for an extended
  // period of time.

  // Do we already have a texture of this size allocated at this address?
  const u64 texture_key = (u64)key;

  {
    std::lock_guard guard(m_cache_lock);
    const auto texture_it = m_texture_key_to_tex.find(texture_key);
    if (texture_it != m_texture_key_to_tex.end()) {
      return texture_it->second;
    }
  }

  // In a stride texture, the actual width is specified by 32*stride register.
  // (stride is already calculated this way when it was passed in)

  // TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO
  // Need to get stride from holly register
  int stride = key.tex_word.stride ? m_console->gpu()->get_text_control_stride() * 32 : 0;
  if (stride)
    width = stride;

  std::shared_ptr<Texture> new_texture = std::make_shared<Texture>();
  new_texture->data.reset(new u32[width * height]);

  new_texture->host_texture_id       = 0xFFFFFFFF;
  new_texture->key                   = key;
  new_texture->hash                  = 0xFFFFFFFF;
  new_texture->width                 = width;
  new_texture->height                = height;
  new_texture->stride                = stride;
  new_texture->dc_vram_address       = vram_address;
  new_texture->tex_word              = key.tex_word;
  new_texture->tsp_word              = key.tsp_word;
  new_texture->is_host_allocated     = false;
  new_texture->is_dirty              = true;
  new_texture->last_updated_on_frame = m_console->gpu()->get_render_count();
  new_texture->last_used_on_frame    = new_texture->last_updated_on_frame;

  new_texture->uuid = rand(); // TODO : I just want every handle to always be uniquely
                              // identifiable. Maybe this is a stupid method.

  // Calculate the size of the texture
  const u32 texture_bytes =
    texture_logic::calculate_texture_bytes(key.tex_word, width, height);
  new_texture->dc_bytes = texture_bytes;

  {
    std::lock_guard guard(m_cache_lock);
    m_texture_key_to_tex[texture_key] = new_texture;
  }

  // Make sure there is a watch on the VRAM pages backing this area so that we can track
  // when it is updated. We translate the 64b texture address to 32b space address which
  // is where our actual physical pages are mapped.

  const u32 addr1           = gpu::VRAMAddress64(vram_address).to32().get();
  const u32 length1         = texture_bytes / 2;
  const auto [start1, end1] = get_memtable_bounds(0x0500'0000 | addr1, length1);
  for (u32 i = start1; i < end1; ++i) {
    if (m_memory_map[i]++ == 0u) {
      m_console->memory()->add_watch(m_memory_handle, i, 1);
    }
  }

  const u32 addr2           = gpu::VRAMAddress64(vram_address + 4).to32().get();
  const u32 length2         = texture_bytes / 2;
  const auto [start2, end2] = get_memtable_bounds(0x0500'0000 | addr2, length2);
  for (u32 i = start2; i < end2; ++i) {
    if (m_memory_map[i]++ == 0u) {
      m_console->memory()->add_watch(m_memory_handle, i, 1);
    }
  }

  return new_texture;
}

std::tuple<u32, u32>
TextureManager::get_memtable_bounds(u32 address, u32 length)
{
  const u32 end              = address + length;
  const u32 from_page        = address / fox::MemoryTable::PAGE_SIZE;
  const u32 first_page_after = (end / fox::MemoryTable::PAGE_SIZE) +
                               ((end & fox::MemoryTable::PAGE_MASK) == 0 ? 0u : 1u);
  return { from_page, first_page_after };
}

void
TextureManager::serialize(serialization::Snapshot &snapshot)
{
  // Save data needed to reconstruct the handle map
  // Save memory watches

  ;
}

void
TextureManager::deserialize(const serialization::Snapshot &snapshot)
{
  // Reconstruct handle map
  // Remove any existing
  //

  ;
}

void
TextureManager::memory_dirtied(u32 address32, u32 length)
{
  std::lock_guard guard(m_cache_lock);

  // The address coming in here is relative to the 32-bit area 0x0500'0000/0x0700'0000.
  // Strip the global address part. We only want the offset into vram.
  address32 &= 0x007f'ffff;

  const u32 write_start = address32;

  // A write just happened to VRAM. See if this overlaps any texture handles and update
  // our accounting of which frame they were last modified.

  for (auto it = m_texture_key_to_tex.begin(); it != m_texture_key_to_tex.end(); ++it) {
    const Texture &tex = *it->second.get();

    it->second->last_updated_on_frame = m_console->gpu()->get_render_count();
    continue;

    const u32 start1 = gpu::VRAMAddress64(tex.dc_vram_address).to32().get();
    const u32 end1   = start1 + tex.dc_bytes / 2;
    const bool hit1  = write_start >= start1 && write_start <= end1;
    if (hit1) {
      it->second->last_updated_on_frame = m_console->gpu()->get_render_count();
    }

    const u32 start2 = gpu::VRAMAddress64(tex.dc_vram_address + 4).to32().get();
    const u32 end2   = start2 + tex.dc_bytes / 2;
    const bool hit2  = write_start >= start2 && write_start <= end2;
    if (hit2) {
      it->second->last_updated_on_frame = m_console->gpu()->get_render_count();
    }

    // if (tex.width == 320 && tex.height == 256) {
    //   printf("memory-dirty addr32 0x%08x length %u tex64 0x%08x\n",
    //          address32,
    //          length,
    //          tex.dc_vram_address);
    //   printf(
    //     " - span1 (32-area) : [0x%08x, 0x%08x) %c\n", start1, end1, hit1 ? '!' : ' ');
    //   printf(
    //     " - span2 (32-area) : [0x%08x, 0x%08x) %c\n", start2, end2, hit2 ? '!' : ' ');
    // }
  }
}
