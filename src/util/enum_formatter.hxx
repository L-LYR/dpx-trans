#pragma once

#include <type_traits>

namespace dpx {

template <typename T>
inline constexpr std::underlying_type_t<T> to_underlying(T t) {
  return static_cast<std::underlying_type_t<T>>(t);
}

}  // namespace dpx

#define EnumFormatter(enum_type, ...)                                                         \
  template <>                                                                                 \
  struct std::formatter<enum_type> : std::formatter<const char *> {                           \
    static constexpr const char *__enum_strs__[] = {__VA_ARGS__};                             \
    template <typename Context>                                                               \
    Context::iterator format(enum_type e, Context out) const {                                \
      return std::formatter<const char *>::format(__enum_strs__[dpx::to_underlying(e)], out); \
    }                                                                                         \
  }