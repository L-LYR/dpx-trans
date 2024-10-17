#pragma once
#include <format>

template <typename... Args>
inline void die(std::string_view fmt, Args &&...args) {
  throw std::runtime_error(std::vformat(fmt, std::make_format_args(args...)));
}

template <>
inline void die<>(std::string_view message) {
  throw std::runtime_error(message.data());
}