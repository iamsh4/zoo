#include <imgui.h>
#include "systems/ps1/gui/gpu.h"
#include "systems/ps1/console.h"

namespace zoo::ps1::gui {

GPU::GPU(Console *console, SharedData *shared_data)
  : ::gui::Window("GPU"),
    m_console(console),
    m_shared_data(shared_data)
{
}

void
GPU::render()
{
  using Word = gpu::GP0OpcodeData::Word;
  using Flags = gpu::GP0OpcodeData::Flags;

  bool something_highlighted = false;

  ImGui::Begin("GPU Debug");

  const char *frame_ids[] = { "frame1", "frame2", "frame3", "frame4", "frame5" };
  std::array<zoo::ps1::GPU::GPUFrameDebugData, 5> frame_data_sets;
  const u32 total =
    m_console->gpu()->frame_data(frame_data_sets.data(), frame_data_sets.size());

  ImGui::Text("GPU Frame data: %u", total);
  for (u32 i = 0; i < total; ++i) {
    const auto &frame_data(frame_data_sets[i]);
    if (ImGui::TreeNode(frame_ids[i],
                        "Frame %u (%u Commands)",
                        frame_data.frame,
                        u32(frame_data.command_buffers.size()))) {

      u32 cmd_num = 0;
      for (const auto &cmd : frame_data.command_buffers) {
        const u8 opcode = cmd.opcode();

        if (cmd.opcode_data.flags & Flags::RenderPolygon) {
          ImGui::Text("%-3u GP0(0x%02x) Render %s%sPolygon",
                      cmd_num,
                      opcode,
                      (cmd.opcode_data.flags & Flags::Shaded) ? "Shaded " : "Monochrome ",
                      (cmd.opcode_data.flags & Flags::Textured) ? "Textured " : "");
        } else {
          ImGui::Text(
            "%-3u GP0(0x%02x) '%s'", cmd_num, opcode, gpu::gp0_opcode_name(opcode));
        }

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();

          // Show flags
          {
            ImGui::Text("Flags: ");
#define FLAG(__flag)                                                                     \
  if (cmd.opcode_data.flags & Flags::__flag) {                                           \
    ImGui::SameLine();                                                                   \
    ImGui::Text("%s", #__flag);                                                          \
  }

            FLAG(RenderPolygon)
            FLAG(RenderLine)
            FLAG(RenderRectangle)
            FLAG(Textured)
            FLAG(Shaded)
            FLAG(PolyLine)
            FLAG(Opaque)
            FLAG(SizeVariable)
            FLAG(Size1)
            FLAG(Size8)
            FLAG(Size16)
            FLAG(TextureBlend)

#undef FLAG
          }

          std::vector<VRAMCoord> coords;

          for (u32 wi = 0; wi < cmd.words.size() && wi < cmd.opcode_data.words.size();
               ++wi) {
            const u32 word = cmd.words[wi];
            const Word word_type = cmd.opcode_data.words[wi];

            const ImColor GREY = ImColor(0xffcccccc);

            if (word_type == Word::ColorCommand) {
              const u8 r = (word >> 0) & 0xff;
              const u8 g = (word >> 8) & 0xff;
              const u8 b = (word >> 16) & 0xff;
              ImGui::TextColored(
                GREY, "0x%08x : ColorCommand (r=%u, g=%u, b=%u)", word, r, g, b);
            } else if (word_type == Word::Color) {
              const u8 r = (word >> 0) & 0xff;
              const u8 g = (word >> 8) & 0xff;
              const u8 b = (word >> 16) & 0xff;
              ImGui::TextColored(
                GREY, "0x%08x : Color        (r=%u, g=%u, b=%u)", word, r, g, b);
            } else if (word_type == Word::NotModeled) {
              ImGui::TextColored(GREY, "0x%08x : (Unmodeled)", word);
            } else if (word_type == Word::TexCoord) {
              gpu::TexCoordPalette param { .raw = word };
              ImGui::TextColored(
                GREY, "0x%08x : TexCoord     (x=%u, y=%u)", word, param.x, param.y);
            } else if (word_type == Word::TexCoordPage) {
              gpu::TexCoordTexPage param { .raw = word };
              ImGui::TextColored(
                GREY,
                "0x%08x : TexCoordPage (x=%u, y=%u, texpage_x=%u, texpage_y=%u, "
                "blending_mode=%u, color_mode=%u)",
                word,
                param.x,
                param.y,
                (param.texpage & 0xf) * 64,
                ((param.texpage >> 4) & 0x1) * 256,
                (param.texpage >> 5) & 0x3,
                (param.texpage >> 7) & 0x3);
                
            } else if (word_type == Word::TexCoordPallete) {
              // TODO : decode clut coordinates
              gpu::TexCoordPalette param { .raw = word };

              const u32 clut_x = (param.clut & 0x3f) * 16;
              const u32 clut_y = (param.clut >> 6) & 0x1ff;

              ImGui::TextColored(
                GREY,
                "0x%08x : TexCoordPal  (x=%u, y=%u, clut=0x%x [x=%u,y=%u])",
                word,
                param.x,
                param.y,
                param.clut,
                clut_x,
                clut_y);

            } else if (word_type == Word::Vertex) {
              gpu::VertexXY param { .raw = word };
              ImGui::TextColored(
                GREY, "0x%08x : Vertex       (x=%d, y=%d)", word, param.x, param.y);
              coords.push_back({ param.x, param.y });

            } else if (word_type == Word::WidthHeight) {
              struct {
                u16 width;
                u16 height;
              } param;
              memcpy(&param, &word, sizeof(word));
              ImGui::TextColored(GREY,
                                 "0x%08x : Size         (w=%u, h=%u)",
                                 word,
                                 param.width,
                                 param.height);

            } else {
              ImGui::TextColored(
                ImColor(0xff0000ff), "0x%08x : No formatter yet", cmd.words[wi]);
            }
          }
          ImGui::EndTooltip();

          // Show highlighted polygon in VRAM viewer
          if (!coords.empty()) {
            m_shared_data->set_vram_coords(coords);
            something_highlighted = true;
          }
        }

        cmd_num++;
      }

      ImGui::TreePop();
    }
  }

  if (!something_highlighted) {
    m_shared_data->set_vram_coords({});
  }

  ImGui::End();
}
}
