#pragma once

#include <type_traits>

namespace dpx {

template <typename T1, typename T2>
  requires(std::is_integral_v<T1> && std::is_integral_v<T2>)
inline T1 upper_align(T1 x, T2 align) {
  assert(align > 0);
  return (x + align - 1) / align * align;
}

}  // namespace dpx
