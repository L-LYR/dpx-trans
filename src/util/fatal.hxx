#pragma once

#include <format>
#include <iostream>
#include <source_location>

namespace dpx::trans {

namespace details {
inline std::string to_string(const std::source_location& l) {
  return std::format("{}:{} `{}`: ", l.file_name(), l.line(), l.function_name());
}
}  // namespace details

struct Footprint {
  // TODO: can we redirect the trace log to file?
  inline static void trace(std::string what) { std::cerr << what << '\n'; }
};

[[noreturn]] inline void die(std::string why) { throw std::runtime_error(why); }

#define die(fmt, ...)                                                               \
  dpx::trans::die(dpx::trans::details::to_string(std::source_location::current()) + \
                  std::vformat(fmt, std::make_format_args(__VA_ARGS__)))

#ifdef ENABLE_FOOTPRINT
#define footprint(fmt, ...)                                                                      \
  dpx::trans::Footprint::trace(dpx::trans::details::to_string(std::source_location::current()) + \
                               std::vformat(fmt, std::make_format_args(__VA_ARGS__)))
#else
#define footprint(fmt, ...) (void)
#endif

}  // namespace dpx::trans
