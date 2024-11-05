#pragma once

#include <bits/types/struct_iovec.h>
#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

template <typename BufferType>
concept ByteView = zpp::bits::concepts::byte_view<BufferType>;

template <bool own>
class BufferBase {
 public:
  // for zpp_bits inner traits
  using value_type = uint8_t;

  BufferBase() {}

  BufferBase(uint8_t *buf_, size_t len_)
    requires(!own)
      : base(buf_), len(len_) {
    clear();
  }
  explicit BufferBase(size_t len_)
    requires(own)
      : base(new uint8_t[len_]), len(len_) {
    clear();
  }
  BufferBase(const BufferBase &other)
    requires(!own)
  {
    base = other.base;
    len = other.len;
  }
  BufferBase &operator=(const BufferBase &other)
    requires(!own)
  {
    base = other.base;
    len = other.len;
  }
  BufferBase(BufferBase &other)
    requires(own)
  = delete;
  BufferBase &operator=(BufferBase &other)
    requires(own)
  = delete;
  BufferBase(BufferBase &&other) {
    base = std::exchange(other.base, nullptr);
    len = std::exchange(other.len, -1);
  }
  BufferBase &operator=(BufferBase &&other) {
    if (this != &other) {
      free();
      base = std::exchange(other.base, nullptr);
      len = std::exchange(other.len, -1);
    }
    return *this;
  }
  ~BufferBase() { free(); }

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
  void free() {
    if constexpr (own) {
      if (base != nullptr) {
        delete[] base;
      }
    }
  }

  uint8_t *base = nullptr;
  size_t len = -1;
};

using OwnedBuffer = BufferBase<true>;
using BorrowedBuffer = BufferBase<false>;

static_assert(!std::is_copy_assignable_v<OwnedBuffer>, "OwnedBuffer is not copy assginable");
static_assert(!std::is_copy_constructible_v<OwnedBuffer>, "OwnedBuffer is not copy constructible");
static_assert(ByteView<OwnedBuffer>, "Buffer is not a valid byte view");
static_assert(std::is_copy_assignable_v<BorrowedBuffer>, "BorrowedBuffer is copy assginable");
static_assert(std::is_copy_constructible_v<BorrowedBuffer>, "BorrowedBuffer is copy constructible");
static_assert(ByteView<BorrowedBuffer>, "Buffer is not a valid byte view");

namespace naive {

template <typename Buffer>
class BuffersBase : public OwnedBuffer {
 public:
  using BufferType = Buffer;

  BuffersBase(size_t n, size_t piece_len_)
    requires(!std::is_constructible_v<Buffer, uint8_t *, size_t>)
      : OwnedBuffer(piece_len_ * n), piece_len(piece_len_) {}

  BuffersBase(size_t n, size_t piece_len_)
    requires(std::is_constructible_v<Buffer, uint8_t *, size_t>)
      : OwnedBuffer(piece_len_ * n), piece_len(piece_len_) {
    uint8_t *p = base;
    for (auto i = 0uz; i < n; ++i) {
      handles.emplace_back(p, piece_len);
      p += piece_len;
    }
  }

  ~BuffersBase() = default;

  BuffersBase(BuffersBase &&other) : OwnedBuffer(std::move(other)) {
    piece_len = std::exchange(other.piece_len, -1);
    handles = std::exchange(other.handles, std::vector<BufferType>{});
  }

  BuffersBase &operator=(BuffersBase &&other) = delete;

  size_t n_elements() const { return handles.size(); }
  size_t piece_size() const { return piece_len; }
  BufferType &operator[](size_t index) { return handles[index]; }
  const BufferType &operator[](size_t index) const { return handles[index]; }

 protected:
  size_t piece_len = -1;
  std::vector<BufferType> handles;
};

using Buffers = BuffersBase<BorrowedBuffer>;

}  // namespace naive
