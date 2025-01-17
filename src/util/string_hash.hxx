#pragma once

#include <cassert>
#include <string>
#include <string_view>

// for heterogeneous lookup of unordered_map
struct string_hash {
  using hash_type = std::hash<std::string_view>;
  using is_transparent = void;

  size_t operator()(const char *str) const { return hash_type{}(str); }
  size_t operator()(std::string_view str) const { return hash_type{}(str); }
  size_t operator()(const std::string &str) const { return hash_type{}(str); }
};
