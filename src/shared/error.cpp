#include <cstdlib>
#include <cstdio>
#include <exception>

void
___check(bool condition,
         const char *file,
         int line,
         const char *func,
         const char *message)
{
  if (!condition) {
    fprintf(stderr, "Assertion failed: %s:%d: %s: %s\n", file, line, func, message);
    std::terminate();
  }
}