#include <cstdio>
#include "webgpu.h"

int
main(int argc, char **argv)
{
  WGPUInstanceDescriptor desc = {};
  WGPUInstance instance = wgpuCreateInstance(&desc);
  printf("Hello %p\n", instance);
  return 0;
}