#ifdef PROFILE_MEMORY

#define TRACY_ENABLE
#include <tracy/Tracy.hpp>

void *
operator new(std ::size_t count)
{
  auto ptr = malloc(count);
  TracyAllocS(ptr, count, 20);
  return ptr;
}
void
operator delete(void *ptr) noexcept
{
  TracyFreeS(ptr, 20);
  free(ptr);
}

#endif
