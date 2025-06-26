#pragma once

#include <vector>
#include "renderer/vulkan.h"

#include "shared/types.h"

namespace zoo::ps1 {

class Renderer {
public:
  // In order to handle the general case...
  // 1)  Renderer receives a copy of vram from the system through copy_from_vram()
  // 2) ... PS1 GPU issues draw commands to renderer ...
  // 3) When the system needs latest render results, system calls copy_to_vram()

  struct Pos {
    i16 x;
    i16 y;
  };

  struct DrawingArea {
    u32 top_left;
    u32 bottom_right;
  };

  struct GPUState {
    DrawingArea drawing_area;
    i32 drawing_offset;
  };

  struct CmdDrawTriangle {
    Pos pos1;
    Pos pos2;
    Pos pos3;

    // FLAGS B G R
    u32 color1 = 0xffffffff;
    u32 color2 = 0xffffffff;
    u32 color3 = 0xffffffff;

    Pos tex1;
    Pos tex2;
    Pos tex3;

    u16 tex_page = 0;
    u16 clut_xy = 0;
    u8 opcode = 0;
  };

  struct CmdUpdateGPUState {
    GPUState gpu_state;
  };

  struct CmdSetUniforms {};

  enum class DrawCmdType
  {
    Triangle = 0,
    SetUniforms = 1,
    UpdateGPUState = 2,
  };

  struct DrawCmd {
    DrawCmdType type;
    union {
      CmdDrawTriangle cmd_draw_triangle;
      CmdSetUniforms cmd_set_uniforms;
      CmdUpdateGPUState cmd_update_gpu_state;
    };
  };

  u8 m_vram[1024 * 512 * 2];

private:
  std::vector<DrawCmd> m_commands;

  Vulkan *m_vulkan;

  struct GPUVertexData {
    float x;
    float y;
    float u;
    float v;
    u32 color;

    u32 texpage_clut;
    union Flags {
      struct {
        u32 opcode : 8;
        u32 _unused : 24;
      };
      u32 raw;
    } flags;
  };

  GPUState m_gpu_state = {};

  GPUVertexData *m_polygon_data_base;
  u32 m_current_vertex_count;

  VmaAllocator m_vma_allocator;

  VkCommandBuffer m_command_buffer;
  VkFence m_fence;
  VkRenderPass m_renderpass;
  VkFramebuffer m_framebuffer;
  VkSampler m_sampler;

  VkPipelineLayout m_polygon_pipeline_layout;
  VkPipeline m_polygon_pipeline;

  VkDescriptorSetLayout m_polygon_descriptor_set_layout;
  VkDescriptorPool m_descriptor_pool;
  VkDescriptorSet m_polygon_pipeline_descriptor_set;

  //
  VmaAllocation m_pixbuf_allocation;
  VkBuffer m_pixbuf;
  u16 *m_pixbuf_mapped;

  //
  VkBuffer m_polygon_buffer;
  VmaAllocation m_polygon_buffer_allocation;

  //
  VkShaderModule m_vertex_shader;
  VkShaderModule m_fragment_shader;
  VkPipeline m_graphics_pipeline;

  //
  VmaAllocation m_vram_read_image_allocation;
  VkImage m_vram_read_image;
  VkImageView m_vram_read_imageview;
  u32 *m_vram_read_image_mapped;

  //
  VmaAllocation m_vkimage_allocation;
  VkImage m_vkimage;
  VkImageView m_vkimageview;
  u32 *m_vkimage_mapped;

  VmaAllocation m_transfer_buffer_allocation;
  VkBuffer m_transfer_buffer;

  void synchronous_cmd_begin();
  void synchronous_cmd_end_and_submit(VkPipelineStageFlags);

  void init();
  void perform_pending_draws();

  void compile_shader(const char *spirv_path, VkShaderModule *module);

public:
  Renderer(Vulkan *);
  ~Renderer();

  void sync_gpu_to_renderer(u8 *src);
  void sync_renderer_to_gpu(u8 *dest);

  void push_triangle(const CmdDrawTriangle &);
  void push_uniforms(const CmdSetUniforms &);
  void update_gpu_state(const CmdUpdateGPUState &);
};

} // namespace systems::ps1
