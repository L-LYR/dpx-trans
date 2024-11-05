#pragma once

#include <bits/types/struct_iovec.h>
#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <span>

#include "util/logger.hxx"
#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

template <typename BufferType>
concept ByteView = zpp::bits::concepts::byte_view<BufferType>;

class BufferBase {
 public:
  BufferBase() = default;
  BufferBase(uint8_t *base_, size_t len_) : base(base_), len(len_) {}
  ~BufferBase() = default;

  // for zpp_bits inner traits
  using value_type = uint8_t;

  uint8_t *data() { return base; }
  const uint8_t *data() const { return base; }
  size_t size() const { return len; }
  uint8_t &operator[](size_t i) {
    assert(i >= 0 && i < len);
    return base[i];
  }
  uint8_t operator[](size_t i) const {
    assert(i >= 0 && i < len);
    return base[i];
  }

  void clear() { memset(base, 0, len); }

  operator std::span<uint8_t>() { return std::span<uint8_t>(base, len); }
  operator std::span<const uint8_t>() const { return std::span<uint8_t>(base, len); }
  operator iovec() const { return iovec{.iov_base = reinterpret_cast<void *>(base), .iov_len = len}; }

 protected:
  uint8_t *base = nullptr;
  size_t len = -1;
};

class OwnedBuffer : public BufferBase, Noncopyable, Nonmovable {
 public:
  explicit OwnedBuffer(size_t size) : BufferBase(new uint8_t[size], size) {
    TRACE("OwnedBuffer at {} with length {}", (void *)base, len);
  }
  ~OwnedBuffer() {
    if (base != nullptr) {
      delete[] base;
    }
  }
};

class BorrowedBuffer : public BufferBase {
 public:
  BorrowedBuffer(uint8_t *base, size_t len) : BufferBase(base, len) {}
  ~BorrowedBuffer() = default;
};

static_assert(ByteView<OwnedBuffer>, "Buffer is not a valid byte view");
static_assert(ByteView<BorrowedBuffer>, "Buffer is not a valid byte view");

namespace naive {

class Buffers : public OwnedBuffer {
 public:
  using BufferType = BorrowedBuffer;

  Buffers(size_t n, size_t piece_len_) : OwnedBuffer(piece_len_ * n), piece_len(piece_len_) {
    for (auto p = base; p < base + len; p += piece_len) {
      handles.emplace_back(p, piece_len);
    }
  }

  ~Buffers() = default;

  size_t n_elements() const { return handles.size(); }
  size_t piece_size() const { return piece_len; }
  BufferType &operator[](size_t index) { return handles[index]; }
  const BufferType &operator[](size_t index) const { return handles[index]; }

 protected:
  size_t piece_len = -1;
  std::vector<BufferType> handles;
};

}  // namespace naive
