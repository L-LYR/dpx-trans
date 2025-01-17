#include "util/hex_dump.hxx"
#include <iomanip>
#include <sstream>

namespace dpx {

std::string Hexdump::to_string(size_t row_size, bool show_ascii) const {
  std::stringstream out;
  out.fill('0');
  out << "Address: " << std::uppercase << std::hex << "0x"
      << reinterpret_cast<uintptr_t>(s.data()) << "\n"
      << "Length:  " << std::dec << s.size() << '\n'
      << "Content: ";
  for (auto i = 0uz; i < s.size(); i += row_size) {
    out << '\n' << std::setw(8) << std::hex << i << ": ";
    for (auto j = 0uz; j < row_size; ++j) {
      auto k = i + j;
      if (k < s.size()) {
        out << hex_lookup[s[i + j] >> 4] << hex_lookup[s[i + j] & 0xF];
      } else {
        out << ' ' << ' ';
      }
      if (k % 2 == 1) {
        out << ' ';
      }
    }
    if (show_ascii) {
      out << ' ';
      for (auto j = 0uz; j < row_size && i + j < s.size(); ++j) {
        out << (std::isprint(s[i + j]) ? static_cast<char>(s[i + j]) : '.');
      }
    }
  }
  return out.str();
}

} // namespace dpx
