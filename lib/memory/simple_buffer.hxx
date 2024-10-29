#pragma once

#include <bits/types/struct_iovec.h>
#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

#include "util/noncopyable.hxx"

template <typename BufferType>
concept ByteView = zpp::bits::concepts::byte_view<BufferType>;

template <bool own>
class BufferBase : Noncopyable {
 public:
  // for zpp_bits inner traits
  using value_type = uint8_t;

  BufferBase(std::pair<uint8_t *, size_t> &&p) : BufferBase(p.first, p.second) {}

  BufferBase(uint8_t *buf_, size_t len_)
    requires(not own)
      : buf(buf_), len(len_) {
    clear();
  }
  explicit BufferBase(size_t len_)
    requires(own)
      : buf(new uint8_t[len_]), len(len_) {
    clear();
  }
  BufferBase(BufferBase &&other) {
    buf = std::exchange(other.buf, nullptr);
    len = std::exchange(other.len, -1);
  }
  BufferBase &operator=(BufferBase &&other) {
    if (this != &other) {
      free();
      buf = std::exchange(other.buf, nullptr);
      len = std::exchange(other.len, -1);
    }
    return *this;
  }
  ~BufferBase() { free(); }

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

  operator std::span<uint8_t>() { return std::span<uint8_t>(buf, len); }
  operator std::span<const uint8_t>() const { return std::span<uint8_t>(buf, len); }
  operator iovec() const { return iovec{.iov_base = reinterpret_cast<void *>(buf), .iov_len = len}; }

 protected:
  void free() {
    if constexpr (own) {
      if (buf != nullptr) {
        delete[] buf;
      }
    }
  }

  uint8_t *buf = nullptr;
  size_t len = -1;
};

using OwnedBuffer = BufferBase<true>;
using BorrowedBuffer = BufferBase<false>;

static_assert(ByteView<OwnedBuffer>, "Buffer is not a valid byte view");
static_assert(ByteView<BorrowedBuffer>, "Buffer is not a valid byte view");

template <ByteView ByteView = BorrowedBuffer>
// TODO: change the base class
class Buffers : public std::vector<ByteView>, Noncopyable {
  using Base = std::vector<ByteView>;

 public:
  Buffers(size_t n, size_t piece_len_)
      : total_len(piece_len_ * n), piece_len(piece_len_), base(new uint8_t[total_len]) {
    uint8_t *p = base;
    for (auto i = 0uz; i < n; ++i) {
      this->emplace_back(p, piece_len);
      p += piece_len;
    }
  }

  ~Buffers() { free(); }

  Buffers(Buffers &&other) : Base(std::move(other)) {
    base = std::exchange(other.base, nullptr);
    total_len = std::exchange(other.total_len, -1);
    piece_len = std::exchange(other.piece_len, -1);
  }

  Buffers &operator=(Buffers &&other) {
    if (this != &other) {
      free();
      Base::operator=(std::move(other));
      base = std::exchange(other.base, nullptr);
      total_len = std::exchange(other.total_len, -1);
      piece_len = std::exchange(other.piece_len, -1);
    }
    return *this;
  }

  std::vector<iovec> to_iovec() const { return std::vector<iovec>(Base::begin(), Base::end()); }

  uint8_t *base_address() { return base; }
  const uint8_t *base_address() const { return base; }
  size_t total_length() const { return total_len; }
  size_t piece_length() const { return piece_len; }

 protected:
  void free() {
    if (base != nullptr) {
      delete[] base;
    }
  }

  size_t total_len = -1;
  size_t piece_len = -1;
  uint8_t *base = nullptr;
};
