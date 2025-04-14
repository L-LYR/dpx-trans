#pragma once

#include <bits/types/struct_iovec.h>

#include <cstdint>

#include "util/fatal.hxx"
#include "util/hex_dump.hxx"

namespace dpx::trans {

class MemoryRegion {
  static bool is_overlap(uintptr_t a, size_t a_len, uintptr_t b, size_t b_len) {
    return std::max(a, b) <= std::min(a + a_len, b + b_len);
  }

  static bool is_contain(uintptr_t a, size_t a_len, uintptr_t b, size_t b_len) {
    return (a <= b) && ((b + b_len) <= (a + a_len));
  }

 public:
  MemoryRegion() = default;
  MemoryRegion(uintptr_t base_, size_t len_) : base(base_), len(len_) {}
  MemoryRegion(void *base_, size_t len_) : MemoryRegion(reinterpret_cast<uintptr_t>(base_), len_) {}
  ~MemoryRegion() = default;

  uintptr_t address() const { return base; }
  uint8_t *data() { return reinterpret_cast<uint8_t *>(base); }
  const uint8_t *data() const { return reinterpret_cast<const uint8_t *>(base); }
  size_t size() const { return len; }

  bool empty() const { return base == 0 && len == 0; }

  bool within(const MemoryRegion &other) const { return !empty() && !other.empty() && other.contain(*this); }

  bool contain(const MemoryRegion &other) const {
    return !empty() && !other.empty() && is_contain(base, len, other.base, other.len);
  }

  bool overlap(const MemoryRegion &other) const {
    return !empty() && !other.empty() && is_overlap(base, len, other.base, other.len);
  }

  MemoryRegion sub_region(size_t offset, size_t length) {
    MemoryRegion sub(base + offset, length);
    if (!contain(sub)) {
      die("Fail to get {} from {}", sub, *this);
    }
    return sub;
  }

  bool operator==(const MemoryRegion &other) const { return base == other.base && len == other.len; }

  operator Hexdump() const { return Hexdump(data(), size()); }
  operator std::string_view() const { return std::string_view(reinterpret_cast<const char *>(data()), size()); }
  operator std::span<uint8_t>() { return std::span<uint8_t>(data(), size()); }
  operator std::span<const uint8_t>() const { return std::span<const uint8_t>(data(), size()); }
  operator iovec() { return iovec{.iov_base = reinterpret_cast<void *>(data()), .iov_len = size()}; }

 protected:
  uintptr_t base = 0;
  size_t len = 0;
};

}  // namespace dpx::trans

template <>
struct std::formatter<dpx::trans::MemoryRegion> : std::formatter<std::string> {
  template <typename Context>
  Context::iterator format(const dpx::trans::MemoryRegion &mr, Context out) const {
    return std::formatter<std::string>::format(std::format("memory region: [{} {}]", (void *)mr.data(), mr.size()),
                                               out);
  }
};
