#pragma once

#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

#include "util/noncopyable.hxx"

class Buffer : Noncopyable {
 public:
  Buffer(uint8_t *buf_, size_t len_, size_t idx_) : buf(buf_), len(len_), idx(idx_), own(false) {}
  explicit Buffer(size_t len_) : buf(new uint8_t[len_]), len(len_), own(true) { clear(); }
  Buffer(Buffer &&other) {
    buf = std::exchange(other.buf, nullptr);
    len = std::exchange(other.len, -1);
    idx = std::exchange(other.idx, -1);
    own = std::exchange(other.own, false);
  }
  Buffer &operator=(Buffer &&other) {
    if (this != &other) {
      free();
      buf = std::exchange(other.buf, nullptr);
      len = std::exchange(other.len, -1);
      idx = std::exchange(other.idx, -1);
      own = std::exchange(other.own, false);
    }
    return *this;
  }
  ~Buffer() { free(); }

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
  void free() {
    if (own && buf != nullptr) {
      delete[] buf;
    }
  }

  uint8_t *buf = nullptr;
  size_t len = -1;
  size_t idx = -1;
  bool own;

 public:
  // for zpp_bits inner traits
  using value_type = uint8_t;
};

class Buffers : public std::vector<Buffer>, Noncopyable {
  using Base = std::vector<Buffer>;

 public:
  Buffers(size_t n, size_t piece_len_) : len(piece_len_ * n), piece_len(piece_len_), base(new uint8_t[len]) {
    uint8_t *p = base;
    for (auto i = 0uz; i < n; ++i) {
      this->emplace_back(p, piece_len, i);
      p += piece_len;
    }
  }

  ~Buffers() { free(); }

  Buffers(Buffers &&other) : Base(std::move(other)) {
    base = std::exchange(other.base, nullptr);
    len = std::exchange(other.len, -1);
    piece_len = std::exchange(other.piece_len, -1);
  }

  Buffers &operator=(Buffers &&other) {
    if (this != &other) {
      free();
      Base::operator=(std::move(other));
      base = std::exchange(other.base, nullptr);
      len = std::exchange(other.len, -1);
      piece_len = std::exchange(other.piece_len, -1);
    }
    return *this;
  }

 public:
  uint8_t *base_address() { return base; }
  const uint8_t *base_address() const { return base; }
  size_t length() const { return len; }
  size_t piece_length() const { return piece_len; }

 private:
  void free() {
    if (base != nullptr) {
      delete[] base;
    }
  }

  size_t len = -1;
  size_t piece_len = -1;
  uint8_t *base = nullptr;
};

static_assert(zpp::bits::concepts::byte_view<Buffer>, "Buffer is not a valid byte view");
