#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "fox/memtable.h"
#include "gpu/texture.h"
#include "shared/types.h"
#include "shared/log.h"
#include "serialization/serializer.h"
#include "gpu/ta_commands.h"

class Console;

namespace gpu {

// A context for all textures in VRAM. Understands various texture data in VRAM
// and converts that data to a uniform RGBA format. Has ownership of host texture
// resources.
class TextureManager : public fox::MemoryWatcher, serialization::Serializer {
public:
  TextureManager(Console *console);
  ~TextureManager();

  std::vector<std::pair<u32, std::shared_ptr<Texture>>> get_vram_to_textures();

  /** Get a handle of a texture for the given texture format/address. */
  std::shared_ptr<Texture> get_texture_handle(TextureKey key);

  void invalidate_all();
  void callback_pre_render();
  void callback_post_render();

  void serialize(serialization::Snapshot &snapshot) final;
  void deserialize(const serialization::Snapshot &snapshot) final;

private:
  static Log::Logger<Log::LogModule::GRAPHICS> log;

  Console *const m_console;

  /*!
   * @brief Reference to the virtual memory range where the guest CPU's native
   *        instructions are stored. This is used to respond to overwrites of
   *        the source instructions and invalidate cache entries.
   */
  fox::MemoryTable *const m_guest_memory;

  /*!
   * @brief Our handle for creating memory watches in guest memory.
   */
  const fox::MemoryTable::WatcherHandle m_memory_handle;

  /*!
   * @brief Table keeping track of how many active textures are present in
   *        each page (fox::MemoryTable::PAGE_SIZE) of guest physical memory. Entries
   *        with non-zero value are registered to watch for memory writes to
   *        guest memory.
   */
  std::vector<u8> m_memory_map;

  std::unordered_map<u64, std::shared_ptr<Texture>> m_texture_key_to_tex;

  mutable std::recursive_mutex m_cache_lock;

  std::tuple<u32, u32> get_memtable_bounds(u32 address, u32 length);
  void memory_dirtied(u32 address, u32 length) override;
};

}
