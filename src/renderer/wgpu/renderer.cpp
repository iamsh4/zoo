#include <thread>
#include <vector>

#include "shared/types.h"
#include "renderer.h"

#include <wgpu.h>

namespace zoo::wgpu {

enum class AwaitState
{
  Init,
  Success,
  Error
};

template<typename T>
struct Await {
  T value;
  AwaitState state = AwaitState::Init;
  void wait(WGPUDevice device)
  {
    while (state == AwaitState::Init) {
      if (device) {
        wgpuDevicePoll(device, false, nullptr);
      }
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  }
};

Renderer::~Renderer()
{
  if (_device) {
    wgpuDeviceRelease(_device);
  }
  if (_adapter) {
    wgpuAdapterRelease(_adapter);
  }
  if (_instance) {
    wgpuInstanceRelease(_instance);
  }
}

WGPUAdapter
Renderer::get_adapter()
{
  WGPURequestAdapterOptions options = {};

  Await<WGPUAdapter> adapter_await;
  WGPUInstanceRequestAdapterCallback adapterCallback = [](WGPURequestAdapterStatus status,
                                                          WGPUAdapter adapter,
                                                          const char *message,
                                                          void *userdata) -> void {
    Await<WGPUAdapter> *await = static_cast<Await<WGPUAdapter> *>(userdata);
    if (status != WGPURequestAdapterStatus_Success) {
      fprintf(stderr, "Failed to request WGPU adapter: %s\n", message);
      await->state = AwaitState::Error;
      return;
    } else {
      await->value = adapter;
      await->state = AwaitState::Success;
    }
  };
  fprintf(stderr, "Acquiring WGPU adapter\n");
  wgpuInstanceRequestAdapter(_instance, &options, adapterCallback, &adapter_await);
  adapter_await.wait(nullptr);
  if (adapter_await.state == AwaitState::Error) {
    throw std::runtime_error("Failed to request WGPU adapter");
  }

  return adapter_await.value;
}

WGPUDevice
Renderer::get_device()
{
  WGPURequiredLimits required_limits                       = {};
  required_limits.limits.maxComputeWorkgroupSizeX          = 32;
  required_limits.limits.maxComputeWorkgroupSizeY          = 32;
  required_limits.limits.maxComputeWorkgroupSizeZ          = 1;
  required_limits.limits.maxComputeInvocationsPerWorkgroup = 1024;
  required_limits.limits.minUniformBufferOffsetAlignment   = 256;
  required_limits.limits.minStorageBufferOffsetAlignment   = 256;
  required_limits.limits.maxBindGroups                     = 4;
  required_limits.limits.maxBufferSize                     = 64 * 1024 * 1024;
  required_limits.limits.maxBindingsPerBindGroup           = 8;
  required_limits.limits.maxStorageBuffersPerShaderStage   = 8;
  required_limits.limits.maxUniformBufferBindingSize       = 64 * 1024 * 1024;
  required_limits.limits.maxStorageBufferBindingSize       = 64 * 1024 * 1024;
  required_limits.limits.maxUniformBuffersPerShaderStage   = 4;
  required_limits.limits.maxComputeWorkgroupsPerDimension  = 1024;
  required_limits.limits.maxTextureDimension2D             = 1024;
  required_limits.limits.maxTextureArrayLayers             = 1;
  required_limits.limits.maxVertexBufferArrayStride        = 16 * sizeof(float);
  required_limits.limits.maxVertexBuffers                  = 2;
  required_limits.limits.maxVertexAttributes               = 4;
  required_limits.limits.maxInterStageShaderComponents     = 16;
  required_limits.limits.maxDynamicStorageBuffersPerPipelineLayout = 1;

  std::vector<WGPUFeatureName> required_features;
  required_features.push_back(WGPUFeatureName_TimestampQuery);

  WGPUDeviceDescriptor device_desc = {};
  device_desc.requiredFeatureCount = required_features.size();
  device_desc.requiredFeatures     = required_features.data();
  device_desc.requiredLimits       = &required_limits;

  Await<WGPUDevice> device_await;
  WGPUAdapterRequestDeviceCallback deviceCallback = [](WGPURequestDeviceStatus status,
                                                       WGPUDevice device,
                                                       const char *message,
                                                       void *userdata) -> void {
    Await<WGPUDevice> *await = static_cast<Await<WGPUDevice> *>(userdata);
    if (WGPURequestDeviceStatus_Success != status) {
      fprintf(stderr, "Failed to request WGPU device: %s\n", message);
      await->state = AwaitState::Error;
      return;
    } else {
      await->value = device;
      await->state = AwaitState::Success;
    }
  };

  fprintf(stderr, "Acquiring WGPU device\n");
  wgpuAdapterRequestDevice(_adapter, &device_desc, deviceCallback, &device_await);
  device_await.wait(nullptr);
  if (device_await.state == AwaitState::Error) {
    throw std::runtime_error("Failed to request WGPU device");
  }

  return device_await.value;
}

Renderer::Renderer()
{
  wgpuSetLogLevel(WGPULogLevel_Error);
  wgpuSetLogCallback([](WGPULogLevel level,
                        const char *message,
                        void *) { fprintf(stderr, "WGPU: %s\n", message); },
                     nullptr);

  WGPUInstanceDescriptor desc = {};

#if defined ZOO_OS_LINUX
  WGPUInstanceExtras extras = {};
  extras.chain.sType        = (WGPUSType)WGPUSType_InstanceExtras;
  extras.backends           = WGPUInstanceBackend_Vulkan;
  desc.nextInChain          = &extras.chain;
#endif

  _instance = wgpuCreateInstance(&desc);
  if (!_instance) {
    throw std::runtime_error("Failed to create WGPU instance");
  }

  _adapter = get_adapter();
  if (!_adapter) {
    throw std::runtime_error("Failed to get WGPU adapter");
  }

  WGPUAdapterProperties adapterProperties = {};
  wgpuAdapterGetProperties(_adapter, &adapterProperties);
  printf("Adapter properties:\n");
  printf("  name: %s\n", adapterProperties.name);
  printf("  backendType: %d\n", adapterProperties.backendType);

  WGPUSupportedLimits adapterLimits = {};
  if (!wgpuAdapterGetLimits(_adapter, &adapterLimits)) {
    throw std::runtime_error("Failed to get adapter limits");
  }
  printf("Adapter limits:\n");
  printf("  maxBindGroups: %u\n", adapterLimits.limits.maxBindGroups);
  printf("  maxBufferSize: %lu MB\n", adapterLimits.limits.maxBufferSize / 1024 / 1024);
  printf("  maxComputeInvocationsPerWorkgroup: %u\n",
         adapterLimits.limits.maxComputeInvocationsPerWorkgroup);
  printf("  maxComputeWorkgroupStorageSize: %u\n",
         adapterLimits.limits.maxComputeWorkgroupStorageSize);

  _device = get_device();

  _queue = wgpuDeviceGetQueue(_device);
  if (!_queue) {
    throw std::runtime_error("Failed to get WGPU queue");
  }
}

WGPUShaderModule
Renderer::create_shader_module(std::string_view label, std::string_view code)
{
  WGPUShaderModuleWGSLDescriptor shader_wgsl_desc = {};
  shader_wgsl_desc.chain.sType                    = WGPUSType_ShaderModuleWGSLDescriptor;
  shader_wgsl_desc.code                           = code.data();

  WGPUShaderModuleDescriptor shader_desc = {};
  shader_desc.label                      = label.data();
  shader_desc.nextInChain                = &shader_wgsl_desc.chain;

  WGPUShaderModule module = wgpuDeviceCreateShaderModule(_device, &shader_desc);
  if (!module) {
    throw std::runtime_error("Failed to create shader module");
  }
  return module;
}

WGPUCommandEncoder
Renderer::create_encoder(std::string_view label)
{
  WGPUCommandEncoderDescriptor encoder_desc = {};
  encoder_desc.label                        = label.data();

  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(_device, &encoder_desc);
  if (!encoder) {
    throw std::runtime_error("Failed to create command encoder");
  }
  return encoder;
}

WGPUComputePassEncoder
Renderer::create_compute_pass_encoder(WGPUCommandEncoder encoder, std::string_view label)
{
  WGPUComputePassDescriptor compute_pass_desc = {};
  compute_pass_desc.label                     = label.data();

  WGPUComputePassEncoder pass =
    wgpuCommandEncoderBeginComputePass(encoder, &compute_pass_desc);
  if (!pass) {
    throw std::runtime_error("Failed to create compute pass encoder");
  }
  return pass;
}

WGPUCommandBuffer
Renderer::finish_encoder(WGPUCommandEncoder encoder, std::string_view label)
{
  WGPUCommandBufferDescriptor command_buffer_desc = {};
  command_buffer_desc.label                       = label.data();

  WGPUCommandBuffer command_buffer =
    wgpuCommandEncoderFinish(encoder, &command_buffer_desc);
  if (!command_buffer) {
    throw std::runtime_error("Failed to finish command buffer");
  }
  return command_buffer;
}

void
Renderer::auto_submit(std::string_view label,
                      std::function<void(WGPUCommandEncoder)> func)
{
  WGPUCommandEncoder encoder = create_encoder(label);
  func(encoder);
  WGPUCommandBuffer command_buffer = finish_encoder(encoder, label);
  wgpuQueueSubmit(_queue, 1, &command_buffer);
  wgpuCommandBufferRelease(command_buffer);
}

void
Renderer::auto_compute_pass(std::string_view label,
                            WGPUCommandEncoder encoder,
                            std::function<void(WGPUComputePassEncoder)> func)
{
  WGPUComputePassEncoder pass = create_compute_pass_encoder(encoder, label);
  func(pass);
  wgpuComputePassEncoderEnd(pass);
  wgpuComputePassEncoderRelease(pass);
}

WGPUBuffer
Renderer::create_buffer(std::string_view label, uint32_t usage, uint64_t size)
{
  WGPUBufferDescriptor buffer_desc = {};
  buffer_desc.label                = label.data();
  buffer_desc.size                 = size;
  buffer_desc.usage                = usage;

  WGPUBuffer buffer = wgpuDeviceCreateBuffer(_device, &buffer_desc);
  if (!buffer) {
    throw std::runtime_error("Failed to create buffer");
  }
  return buffer;
}

void
Renderer::sync_wait_idle()
{
  while (!wgpuDevicePoll(_device, false, nullptr)) {
  }
}
} // namespace zoo::wgpu
