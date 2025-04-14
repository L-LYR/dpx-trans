#pragma once

#include <algorithm>
#include <array>
#include <string>

namespace dpx::trans {

template <size_t Size>
struct c_string {
  using base = std::array<char, Size>;
  constexpr c_string() = default;
  consteval c_string(const char (&v)[Size]) { std::copy_n(v, Size, std::begin(value)); }
  constexpr explicit c_string(const char* str, std::size_t size) { std::copy_n(str, size, std::begin(value)); }
  constexpr explicit c_string(std::string_view sv) : c_string(sv.data(), sv.size()) {}
  constexpr auto size() const { return Size - 1; }
  constexpr auto empty() const { return Size == 1; }
  constexpr auto data() const { return value.data(); }
  constexpr auto begin() const { return value.begin(); }
  constexpr auto end() const { return value.end() - 1; }
  constexpr auto cbegin() const { return value.cbegin(); }
  constexpr auto cend() const { return value.cend() - 1; }
  constexpr auto rbegin() const { return value.rbegin() + 1; }
  constexpr auto rend() const { return value.rend(); }
  constexpr char& operator[](size_t index) { return value[index]; }
  constexpr const char& operator[](size_t index) const { return value[index]; }
  constexpr operator std::string_view() const { return std::string_view(data(), size()); }
  constexpr char& at(size_t index) { return value[index]; }
  constexpr const char& at(size_t index) const { return value[index]; }

  std::array<char, Size> value{};
};

}  // namespace dpx::trans
