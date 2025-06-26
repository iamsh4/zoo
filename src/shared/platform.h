#pragma once

#include "shared/profiling.h"

#if defined(ZOO_OS_LINUX) || defined(ZOO_OS_MACOS)
#include <pthread.h>
#endif

namespace platform {
enum class OS
{
  Linux,
  MacOS,
  Windows,
};

enum class Arch
{
  x86_64,
  ARM,
};

static constexpr OS
getBuildOS()
{
#if defined(ZOO_OS_MACOS)
  return OS::MacOS;
#elif defined(ZOO_OS_LINUX)
  return OS::Linux;
#elif defined(ZOO_OS_WINDOWS)
  return OS::Windows;
#else
#error "unsupported OS"
#endif
}

static constexpr Arch
getBuildArchitecture()
{
#if defined(ZOO_ARCH_X86_64)
  return Arch::x86_64;
#elif defined(ZOO_ARCH_ARM)
  return Arch::ARM;
#else
#error "Unsupported architecture"
#endif
}

void
setThreadName(const char *name)
{
#if defined(ZOO_OS_LINUX)
  const pthread_t _self = pthread_self();
  pthread_setname_np(_self, name);
#elif defined(ZOO_OS_MACOS)
  pthread_setname_np(name);
#else
#warning "setThreadName not implemented for this platform"
#endif
  ProfileSetThreadName("Guest Simulation");
}

}
