#pragma once

#include <cstdint>
#include <format>
#include <iomanip>
#include <ostream>
#include <span>

template <size_t RowSize = 16, bool ShowAscii = true> struct CustomHexdump {
  CustomHexdump(void *data, size_t length)
      : s(reinterpret_cast<uint8_t *>(data), length) {}
  std::span<uint8_t> s;
};

namespace std {
template <size_t RowSize, bool ShowAscii>
std::string to_string(const CustomHexdump<RowSize, ShowAscii> &dump) {
  constexpr static char hex_lookup[] = "012345678ABCDEF";
  std::stringstream out;
  auto &s = dump.s;
  out.fill('0');
  for (auto i = 0uz; i < s.size(); i += RowSize) {
    out << std::setw(8) << std::hex << i << ": ";
    for (auto j = 0uz; j < RowSize; ++j) {
      if (i + j < s.size()) {
        out << hex_lookup[s[i + j] >> 4] << hex_lookup[s[i + j] & 0xF] << ' ';
      } else {
        out << ' ' << ' ' << ' ';
      }
    }
    out << ' ';
    if (ShowAscii) {
      for (auto j = 0uz; j < RowSize; ++j) {
        if (i + j < s.size()) {
          out << (std::isprint(s[i + j]) ? static_cast<char>(s[i + j]) : '.');
        }
      }
    }
    out << '\n';
  }
  return out.str();
}
} // namespace std

template <size_t RowSize, bool ShowAscii>
std::ostream &operator<<(std::ostream &out,
                         const CustomHexdump<RowSize, ShowAscii> &dump) {
  return out << std::to_string(dump);
}

template <size_t RowSize, bool ShowAscii>
struct std::formatter<CustomHexdump<RowSize, ShowAscii>> {
  template <typename Context> constexpr Context::iterator parse(Context &ctx) {
    return ctx.end();
  }

  template <typename Context>
  Context::iterator format(CustomHexdump<RowSize, ShowAscii> dump,
                           Context &ctx) const {
    return std::ranges::copy(std::to_string(dump), ctx.out()).out;
  }
};

using Hexdump = CustomHexdump<16, true>;