// vim: expandtab:ts=2:sw=2

#pragma once

#include <system_error>

#ifdef _WIN64
#define FOX_UNREACHABLE()                                                      \
  do {                                                                         \
    /* TODO... */                                                              \
  } while(0);
#else
#define FOX_UNREACHABLE() __builtin_unreachable()
#endif

#ifdef DEBUG
#define FOX_PEDANTIC(x) x
#else
#define FOX_PEDANTIC(x) false
#endif

namespace fox {

/*!
 * @brief Return an std::error_code representing the current value of errno.
 */
inline std::error_code
errcode()
{
  return std::error_code(errno, std::generic_category());
}

/*!
 * @brief Return an std::error_code representing the provided system error code
 *        (errno value).
 */
inline std::error_code
errcode(const int code)
{
  return std::error_code(code, std::generic_category());
}

}
