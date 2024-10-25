#pragma once

#include <format>
#include <source_location>

namespace detail {
inline std::string die_prefix(std::source_location location) {
  return std::format("{}:{} `{}`: ", location.file_name(), location.line(), location.function_name());
}
}  // namespace detail

template <typename... Args>
[[noreturn]] inline void die(std::string_view fmt, std::source_location location, Args &&...args) {
  throw std::runtime_error(detail::die_prefix(location) + std::vformat(fmt, std::make_format_args(args...)));
}

template <>
[[noreturn]] inline void die<>(std::string_view message, std::source_location location) {
  die("{}", location, message);
}

#define die(fmt, ...) die(fmt, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__)
