#pragma once

#include <functional>
#include <string_view>
#include <webgpu.h>

namespace zoo::wgpu {

class Renderer {
public:
  Renderer();
  ~Renderer();

protected:
  WGPUAdapter get_adapter();
  WGPUDevice get_device();

  void sync_wait_idle();

  WGPUShaderModule create_shader_module(std::string_view label, std::string_view code);
  WGPUBuffer create_buffer(std::string_view label, uint32_t usage, uint64_t size);
  WGPUCommandEncoder create_encoder(std::string_view label);
  WGPUComputePassEncoder create_compute_pass_encoder(WGPUCommandEncoder encoder,
                                                     std::string_view label);
  WGPUCommandBuffer finish_encoder(WGPUCommandEncoder encoder, std::string_view label);

  void auto_submit(std::string_view label, std::function<void(WGPUCommandEncoder)>);
  void auto_compute_pass(std::string_view label,
                         WGPUCommandEncoder encoder,
                         std::function<void(WGPUComputePassEncoder)>);

  WGPUInstance _instance = {};
  WGPUDevice _device     = {};
  WGPUAdapter _adapter   = {};
  WGPUQueue _queue       = {};
};

}
