#include <algorithm>
#include <imgui.h>

#if defined(ZOO_OS_MACOS)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "fox/memtable.h"
#include "gui/window_io_activity.h"

namespace gui {

IOActivityWindow::IOActivityWindow(std::shared_ptr<ConsoleDirector> director)
  : Window("IO Activity / Type"),
    m_director(director)
{
  sysmem_texture = std::make_unique<OpenGLImage>(512, 256);
  sysmem_texture->fill(0xff'111111); // RGBA, R least-sig, A most-sig
  sysmem_texture->update_texture();

  texmem_texture = std::make_unique<OpenGLImage>(256, 256);
  texmem_texture->fill(0xff'111111); // RGBA, R least-sig, A most-sig
  texmem_texture->update_texture();

  aicamem_texture = std::make_unique<OpenGLImage>(128, 128);
  aicamem_texture->fill(0xff'111111); // RGBA, R least-sig, A most-sig
  aicamem_texture->update_texture();
}

void
IOActivityWindow::render()
{
  if (!ImGui::Begin("IO Activity Visualizer")) {
    ImGui::End();
    return;
  }

  const Console::MemoryUsage &usage_all = m_director->console()->memory_usage();
  using namespace dreamcast;

  // Assign color based on usage type
  const auto assign_color = [&](OpenGLImage *image,
                                MemoryPageData<dreamcast::MemoryUsage> &data) {
    for (u32 i = 0; i < image->width * image->height && i < data.page_count(); ++i) {
      const auto [usage, age] = data.get_page(i);
      if (age > 16)
        continue;

      switch (usage) {
        // clang-format off
        case MemoryUsage::SH4_Code:          image->pixel_data[i] = 0xffff'ffff; break;
        case MemoryUsage::G1_DiscReadBuffer: image->pixel_data[i] = 0xff77'1111; break;
        case MemoryUsage::G2_AICA_DMA:       image->pixel_data[i] = 0xff11'7711; break;
        case MemoryUsage::AICA_Arm7Code:     image->pixel_data[i] = 0xff11'ff11; break;
        case MemoryUsage::AICA_WaveData:     image->pixel_data[i] = 0xff11'ffff; break;
        case MemoryUsage::GPU_FrameBufferWrite: image->pixel_data[i] = 0xffaa'77ff; break;
        case MemoryUsage::GPU_FrameBufferRead:  image->pixel_data[i] = 0xff11'77ff; break;
        case MemoryUsage::GPU_TA_OPB:        image->pixel_data[i] = 0xff33'33ff; break;
        case MemoryUsage::GPU_Texture:       image->pixel_data[i] = 0xff11'11ff; break;
        default:                             image->pixel_data[i] = 0xff11'1111; break;
        // clang-format on
      }
    }
    image->update_texture();
  };

  // Fades out the color in an image. This gives the impression of writes fading out.
  #if 0
  const auto fade = [](OpenGLImage &image) {
    for (u32 i = 0; i < image.width * image.height; ++i) {
      u32 previous = image.pixel_data[i];

      u32 R = ((previous & 0x000000FF) * 125 / 128) & 0x000000FF;
      u32 G = ((previous & 0x0000FF00) * 125 / 128) & 0x0000FF00;
      u32 B = ((previous & 0x00FF0000) * 125 / 128) & 0x00FF0000;
      u32 A = (0xFF000000);

      const auto max = [](auto a, auto b) {
        return a < b ? b : a;
      };

      R = max(R, 0x0000'0011u);
      G = max(G, 0x0000'1100u);
      B = max(B, 0x0011'0000u);

      image.pixel_data[i] = A | B | G | R;
    }
    image.update_texture();
  };
  #endif

  assign_color(sysmem_texture.get(), *usage_all.ram.get());
  assign_color(texmem_texture.get(), *usage_all.vram.get());

  // fade(*sysmem_texture.get());
  // fade(*texmem_texture.get());
  // fade(*aicamem_texture.get());

  ImGui::Image((void *)(size_t)sysmem_texture->opengl_handle,
               ImVec2 { float(sysmem_texture->width), float(sysmem_texture->height) });

  ImGui::Image((void *)(size_t)texmem_texture->opengl_handle,
               ImVec2 { float(texmem_texture->width), float(texmem_texture->height) });

  ImGui::Image((void *)(size_t)aicamem_texture->opengl_handle,
               ImVec2 { float(aicamem_texture->width), float(aicamem_texture->height) });

  ImGui::End();
}

}
