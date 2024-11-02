#pragma once

#include <doca_buf.h>
#include <doca_buf_pool.h>
#include <doca_mmap.h>

#include "doca/check.hxx"
#include "doca/device.hxx"
#include "memory/simple_buffer.hxx"

class Buffers;

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

class BorrowedBuffer : public ::BorrowedBuffer {
 public:
  BorrowedBuffer(doca_buf* buf_) : buf(buf_) {
    doca_check(doca_buf_get_data(buf, reinterpret_cast<void**>(&base)));
    doca_check(doca_buf_get_data_len(buf, &len));
  }
  ~BorrowedBuffer() { dec_ref(); }
  BorrowedBuffer(BorrowedBuffer&& other) : ::BorrowedBuffer(std::move(other)) {
    buf = std::exchange(other.buf, nullptr);
  }

  BorrowedBuffer& operator=(BorrowedBuffer&& other) = delete;

  void dec_ref() {
    if (buf != nullptr) {
      doca_check(doca_buf_dec_refcount(buf, nullptr));
      buf = nullptr;
    }
  }

 private:
  doca_buf* buf = nullptr;
};

class Buffers : public OwnedBuffer {
 public:
  Buffers(Device& dev, size_t n, size_t piece_len_) : OwnedBuffer(n * piece_len_), piece_len(piece_len_) {
    doca_check(doca_mmap_create(&mmap));
    doca_check(doca_mmap_add_dev(mmap, dev.dev));
    doca_check(doca_mmap_set_permissions(mmap, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
    doca_check(doca_mmap_set_memrange(mmap, base, len));
    doca_check(doca_mmap_start(mmap));
    doca_check(doca_buf_pool_create(n, piece_len, mmap, &pool));
    doca_check(doca_buf_pool_start(pool));
  }
  ~Buffers() {
    doca_check(doca_buf_pool_stop(pool));
    doca_check(doca_buf_pool_destroy(pool));
    doca_check(doca_mmap_stop(mmap));
    doca_check(doca_mmap_destroy(mmap));
  }

  Buffers(Buffers&& other) : OwnedBuffer(std::move(other)) {
    mmap = std::exchange(other.mmap, nullptr);
    pool = std::exchange(other.pool, nullptr);
  }

  Buffers& operator=(Buffers&& other) = delete;

  size_t n_free() const {
    uint32_t n_free = 0;
    doca_check(doca_buf_pool_get_num_free_elements(pool, &n_free));
    return n_free;
  }
  size_t n_elements() const {
    uint32_t n_elements = 0;
    doca_check(doca_buf_pool_get_num_elements(pool, &n_elements));
    return n_elements;
  }
  size_t piece_size() const { return piece_len; }

  std::optional<BorrowedBuffer> acquire_one() {
    if (n_free() == 0) {
      return {};
    }
    doca_buf* buf = nullptr;
    doca_check(doca_buf_pool_buf_alloc(pool, &buf));
    doca_check(doca_buf_set_data_len(buf, piece_len));
    return std::make_optional(BorrowedBuffer(buf));
  }
  void release_one(BorrowedBuffer&& buffer) {
    assert((buffer.size() == piece_len && base <= buffer.data() && buffer.data() + buffer.size() <= base + len));
    buffer.dec_ref();  // explicitly decrease reference counter
  }

 protected:
  size_t piece_len = -1;
  doca_mmap* mmap = nullptr;
  doca_buf_pool* pool = nullptr;
};

}  // namespace doca
