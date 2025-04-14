#pragma once

#include <type_traits>

namespace dpx::trans {

template <typename T>
inline constexpr std::underlying_type_t<T> to_underlying(T t) {
  return static_cast<std::underlying_type_t<T>>(t);
}

}  // namespace dpx::trans

#define Initializer1(enum_type, v, ...) [dpx::trans::to_underlying(enum_type::v)] = #v
#define Initializer2(enum_type, v, ...) Initializer1(enum_type, v), Initializer1(enum_type, __VA_ARGS__)
#define Initializer3(enum_type, v, ...) Initializer1(enum_type, v), Initializer2(enum_type, __VA_ARGS__)
#define Initializer4(enum_type, v, ...) Initializer1(enum_type, v), Initializer3(enum_type, __VA_ARGS__)
#define Initializer5(enum_type, v, ...) Initializer1(enum_type, v), Initializer4(enum_type, __VA_ARGS__)
// add more
#define NInitializer(n, enum_type, ...) Initializer##n(enum_type, __VA_ARGS__)

#define EnumFormatter(n, enum_type, ...)                                                             \
  template <>                                                                                        \
  struct std::formatter<enum_type> : std::formatter<const char *> {                                  \
    static constexpr const char *__enum_strs__[] = {NInitializer(n, enum_type, __VA_ARGS__)};        \
    template <typename Context>                                                                      \
    Context::iterator format(enum_type e, Context out) const {                                       \
      return std::formatter<const char *>::format(__enum_strs__[dpx::trans::to_underlying(e)], out); \
    }                                                                                                \
  }