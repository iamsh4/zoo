#define VMA_IMPLEMENTATION

// Clang and GCC both complain about the VMA code, but this is vendor code that I'm not
// going to change.

// TODO : Have a separate compilation environment during build to not error on warnings
// for third_party code.

#ifdef __clang__
#pragma clang diagnostic ignored "-Wnullability-completeness"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic warning "-Wparentheses"
#else
#endif

// VMA utilizes snprintf but does not include the necessary header
#include <cstdio>

#include "vk_mem_alloc.h"
