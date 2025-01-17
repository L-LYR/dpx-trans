#pragma once

#include <cstdint>
#include <format>
#include <ostream>
#include <span>

namespace dpx::trans {

struct Hexdump {
  inline constexpr static char hex_lookup[] = "0123456789ABCDEF";

  explicit Hexdump(const std::span<uint8_t> s_) : s(s_) {}

  Hexdump(const void *data, size_t length) : s(reinterpret_cast<const uint8_t *>(data), length) {}

  std::string to_string(size_t row_size = 16, bool show_ascii = true) const;

  friend inline std::ostream &operator<<(std::ostream &out, const Hexdump &d) { return out << d.to_string(); }

 private:
  std::span<const uint8_t> s;
};

}  // namespace dpx::trans

template <>
struct std::formatter<dpx::trans::Hexdump> : std::formatter<std::string> {
  template <typename Context>
  Context::iterator format(const dpx::trans::Hexdump &dump, Context &ctx) const {
    // TODO: parse format parameter
    return std::formatter<std::string>::format(dump.to_string(), ctx);
  }
};
