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

namespace comch {

template <Side s>
class Endpoint;

}

class BorrowedBuffer : public ::BorrowedBuffer {
  template <Side s>
  friend class comch::Endpoint;

 public:
  explicit BorrowedBuffer(doca_buf* buf_) : buf(buf_) {
    doca_check(doca_buf_get_data(buf, reinterpret_cast<void**>(&base)));
    doca_check(doca_buf_get_data_len(buf, &len));
  }
  ~BorrowedBuffer() {}
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
  template <Side s>
  friend class comch::Endpoint;

 public:
  Buffers(Device& dev, size_t n, size_t piece_len_) : OwnedBuffer(n * piece_len_), piece_len(piece_len_) {
    doca_check(doca_mmap_create(&m));
    doca_check(doca_mmap_add_dev(m, dev.dev));
    doca_check(doca_mmap_set_permissions(m, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
    doca_check(doca_mmap_set_memrange(m, base, len));
    doca_check(doca_mmap_start(m));
    doca_check(doca_buf_pool_create(n, piece_len, m, &p));
    doca_check(doca_buf_pool_start(p));
  }
  ~Buffers() {
    if (p != nullptr) {
      doca_check(doca_buf_pool_stop(p));
      doca_check(doca_buf_pool_destroy(p));
    }
    if (m != nullptr) {
      doca_check(doca_mmap_stop(m));
      doca_check(doca_mmap_destroy(m));
    }
  }

  Buffers(Buffers&& other) : OwnedBuffer(std::move(other)) {
    m = std::exchange(other.m, nullptr);
    p = std::exchange(other.p, nullptr);
  }

  Buffers& operator=(Buffers&& other) = delete;

  size_t n_free() const {
    uint32_t n_free = 0;
    doca_check(doca_buf_pool_get_num_free_elements(p, &n_free));
    return n_free;
  }
  size_t n_elements() const {
    uint32_t n_elements = 0;
    doca_check(doca_buf_pool_get_num_elements(p, &n_elements));
    return n_elements;
  }
  size_t piece_size() const { return piece_len; }

  std::optional<BorrowedBuffer> acquire_one() {
    if (n_free() == 0) {
      return {};
    }
    doca_buf* buf = nullptr;
    doca_check(doca_buf_pool_buf_alloc(p, &buf));
    doca_check(doca_buf_set_data_len(buf, piece_len));
    INFO("{}", (void*)buf);
    return BorrowedBuffer(buf);
  }
  void release_one(BorrowedBuffer&& buffer) {
    assert((buffer.size() == piece_len && base <= buffer.data() && buffer.data() + buffer.size() <= base + len));
    buffer.dec_ref();  // explicitly decrease reference counter
  }

 protected:
  doca_mmap* mmap() { return m; }

  size_t piece_len = -1;
  doca_mmap* m = nullptr;
  doca_buf_pool* p = nullptr;
};

}  // namespace doca
