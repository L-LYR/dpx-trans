#pragma once

#include <bits/types/struct_iovec.h>
#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <list>
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

  BufferBase() {}

  BufferBase(uint8_t *buf_, size_t len_)
    requires(not own)
      : base(buf_), len(len_) {
    clear();
  }
  explicit BufferBase(size_t len_)
    requires(own)
      : base(new uint8_t[len_]), len(len_) {
    clear();
  }
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

static_assert(ByteView<OwnedBuffer>, "Buffer is not a valid byte view");
static_assert(ByteView<BorrowedBuffer>, "Buffer is not a valid byte view");

using BorrowedBufferQueue = std::list<BorrowedBuffer>;

namespace naive {

class Buffers : public OwnedBuffer {
 public:
  Buffers(size_t n, size_t piece_len_) : OwnedBuffer(piece_len_ * n), piece_len(piece_len_) {
    uint8_t *p = base;
    for (auto i = 0uz; i < n; ++i) {
      q.emplace_back(p, piece_len);
      p += piece_len;
    }
  }

  ~Buffers() = default;

  Buffers(Buffers &&other) : OwnedBuffer(std::move(*this)) {
    piece_len = std::exchange(other.piece_len, -1);
    q = std::exchange(other.q, BorrowedBufferQueue{});
  }

  Buffers &operator=(Buffers &&other) = delete;
  // {
  //   Base::operator=(std::move(other));
  //   if (this != &other) {
  //     free();
  //     Base::operator=(std::move(other));
  //     base = std::exchange(other.base, nullptr);
  //     total_len = std::exchange(other.total_len, -1);
  //     piece_len = std::exchange(other.piece_len, -1);
  //   }
  //   return *this;
  // }

  size_t n_free() const { return q.size(); }
  size_t n_elements() const { return len / piece_len; }
  size_t piece_size() const { return piece_len; }

  std::optional<BorrowedBuffer> acquire_one() {
    if (q.empty()) {
      return {};
    }
    auto buffer = std::move(q.front());
    q.pop_front();
    return std::make_optional(std::move(buffer));
  }

  void release_one(BorrowedBuffer &&buffer) {
    assert((buffer.size() == piece_len && base <= buffer.data() && buffer.data() + buffer.size() <= base + len));
    q.emplace_back(std::move(buffer));
  }

 protected:
  size_t piece_len = -1;
  BorrowedBufferQueue q;
};

}  // namespace naive
