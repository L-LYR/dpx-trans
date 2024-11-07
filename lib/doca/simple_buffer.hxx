#pragma once

#include <doca_buf.h>
#include <doca_buf_pool.h>
#include <doca_mmap.h>

#include "doca/check.hxx"
#include "doca/device.hxx"
#include "memory/simple_buffer.hxx"

namespace doca {

/*
 *
 * doca_buf layout:
 *
 * head   -->            +-------------------+
 *                       | head room         |
 *                       |                   |
 *                       |                   |
 *                       |                   |
 *                       |                   |
 * data   -->            +-------------------+
 *                       | data room         |
 *                       |                   |
 *                       |                   |
 *                       |                   |
 *                       |                   |
 *                       |                   |
 * data + data_len -->   +-------------------+
 *                       | tail room         |
 *                       |                   |
 *                       |                   |
 *                       |                   |
 * head + len      -->   +-------------------+
 *
 */

class BorrowedBuffer : public BufferBase {
  template <Side s>
  friend class comch::Endpoint;
  friend class Buffers;

 public:
  explicit BorrowedBuffer(doca_buf* buf_) : buf(buf_) {
    doca_check(doca_buf_get_data(buf, reinterpret_cast<void**>(&base)));
    doca_check(doca_buf_get_len(buf, &len));
    DEBUG("BufferBase at {} with length {}", (void*)base, len);
  }
  ~BorrowedBuffer() = default;

  void reset() {
    doca_check(doca_buf_reset_data_len(buf));
    BufferBase::reset();
  }

 private:
  doca_buf* buf = nullptr;
};

static_assert(ByteView<BorrowedBuffer>, "Buffer is not a valid byte view");

class Buffers : public OwnedBuffer {
  template <Side s>
  friend class comch::Endpoint;

 public:
  using BufferType = BorrowedBuffer;

  Buffers(Device& dev, size_t n, size_t piece_len_) : OwnedBuffer(n * piece_len_), piece_len(piece_len_) {
    DEBUG("Buffers {} elements with piece length {}", n, piece_len);
    doca_check(doca_mmap_create(&mmap));
    doca_check(doca_mmap_add_dev(mmap, dev.dev));
    doca_check(doca_mmap_set_permissions(mmap, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
    doca_check(doca_mmap_set_memrange(mmap, base, len));
    doca_check(doca_mmap_start(mmap));
    doca_check(doca_buf_pool_create(n, piece_len, mmap, &pool));
    doca_check(doca_buf_pool_start(pool));
    for (auto i = 0uz; i < n; ++i) {
      doca_buf* buf = nullptr;
      doca_check(doca_buf_pool_buf_alloc(pool, &buf));
      handles.emplace_back(buf);
    }
  }

  ~Buffers() {
    for (auto& h : handles) {
      doca_check(doca_buf_dec_refcount(h.buf, nullptr));
    }
    if (pool != nullptr) {
      doca_check(doca_buf_pool_stop(pool));
      doca_check(doca_buf_pool_destroy(pool));
    }
    if (mmap != nullptr) {
      doca_check(doca_mmap_stop(mmap));
      doca_check(doca_mmap_destroy(mmap));
    }
  }

  size_t n_elements() const { return handles.size(); }
  size_t piece_size() const { return piece_len; }
  BufferType& operator[](size_t index) { return handles[index]; }
  const BufferType& operator[](size_t index) const { return handles[index]; }

 protected:
  doca_mmap* mmap = nullptr;
  doca_buf_pool* pool = nullptr;
  size_t piece_len = -1;
  std::vector<BufferType> handles;
};

}  // namespace doca
