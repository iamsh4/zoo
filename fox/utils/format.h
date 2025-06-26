#pragma once

#include <fmt/core.h>

namespace fox {

/*!
 * @brief Alias for string formatting using std::format or an equivalent
 *        implementation.
 */
template <typename... Args>
auto f(Args&&... args) -> decltype(fmt::format(std::forward<Args>(args)...))
{
    return fmt::format(std::forward<Args>(args)...);
}

}
