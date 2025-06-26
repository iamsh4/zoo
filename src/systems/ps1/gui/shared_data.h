#pragma once

#include <mutex>
#include <vector>
#include "shared/types.h"

namespace zoo::ps1 {
class Console;
}

namespace zoo::ps1::gui {

struct VRAMCoord {
  i16 x;
  i16 y;
};

class SharedData {
  std::mutex m_mutex;
  std::vector<VRAMCoord> m_vram_coords;

public:
  void set_vram_coords(const std::vector<VRAMCoord> &coords)
  {
    std::lock_guard lock(m_mutex);
    m_vram_coords = coords;
  }

  void get_vram_coords(std::vector<VRAMCoord> *out)
  {
    if (out) {
      std::lock_guard lock(m_mutex);
      *out = m_vram_coords;
    }
  }
};

}
