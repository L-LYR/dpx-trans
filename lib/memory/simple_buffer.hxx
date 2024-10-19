#pragma once

#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

#include "util/noncopyable.hxx"

class Buffer : Noncopyable {
 public:
  Buffer(size_t len_ = 1024) : buf(new uint8_t[len_]), len(len_) { clear(); }
  Buffer(Buffer &&other) {
    buf = std::exchange(other.buf, nullptr);
    len = std::exchange(other.len, -1);
    idx = std::exchange(other.idx, -1);
  }
  Buffer &operator=(Buffer &&other) {
    if (this != &other) {
      if (buf != nullptr) {
        delete[] buf;
      }
      buf = std::exchange(other.buf, nullptr);
      len = std::exchange(other.len, -1);
      idx = std::exchange(other.idx, -1);
    }
    return *this;
  }
  ~Buffer() {
    if (buf != nullptr) {
      delete[] buf;
    }
  }

  uint8_t *data() { return buf; }
  const uint8_t *data() const { return buf; }
  size_t size() const { return len; }
  uint8_t &operator[](size_t i) {
    assert(i >= 0 && i < len);
    return buf[i];
  }
  uint8_t operator[](size_t i) const {
    assert(i >= 0 && i < len);
    return buf[i];
  }

  void clear() { memset(buf, 0, len); }

  size_t index() const { return idx; }
  void set_index(size_t idx_) { idx = idx_; }

  operator std::span<uint8_t>() { return std::span<uint8_t>(buf, len); }
  operator std::span<const uint8_t>() const { return std::span<uint8_t>(buf, len); }

 private:
  uint8_t *buf = nullptr;
  size_t len = -1;
  size_t idx = -1;

 public:
  // for zpp_bits inner traits
  using value_type = uint8_t;
};

static_assert(zpp::bits::concepts::byte_view<Buffer>, "Buffer is not a valid byte view");
