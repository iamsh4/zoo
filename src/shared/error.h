#pragma once

#ifdef _WIN64
#define UNREACHABLE()                                                                    \
  { /* TODO... */                                                                        \
  }
#else
#define UNREACHABLE() __builtin_unreachable()
#endif

#ifdef DEBUG
#define PEDANTIC(x) x
#else
#define PEDANTIC(x) false
#endif

[[noreturn]] void ___check(bool condition, const char* file, int line, const char* func, const char* message);
#define _check(condition, message) ___check(condition, __FILE__, __LINE__, __func__, message)