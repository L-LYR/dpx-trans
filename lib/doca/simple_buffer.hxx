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

class BorrowedBuffer : public ::BorrowedBuffer {
  template <Side s>
  friend class comch::Endpoint;
  friend class Buffers;

 public:
  explicit BorrowedBuffer(doca_buf* buf_) : buf(buf_) {
    doca_check(doca_buf_get_data(buf, reinterpret_cast<void**>(&base)));
    doca_check(doca_buf_get_data_len(buf, &len));
  }
  ~BorrowedBuffer() = default;

 private:
  doca_buf* buf = nullptr;
};

class Buffers : public naive::BuffersBase<BorrowedBuffer> {
  template <Side s>
  friend class comch::Endpoint;
  using Base = naive::BuffersBase<BorrowedBuffer>;

 public:
  using BufferType = BorrowedBuffer;

  Buffers(Device& dev, size_t n, size_t piece_len_) : Base(n, piece_len_) {
    doca_check(doca_mmap_create(&m));
    doca_check(doca_mmap_add_dev(m, dev.dev));
    doca_check(doca_mmap_set_permissions(m, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
    INFO("{} {} {} {}", (void*)base, n, len, piece_len);
    doca_check(doca_mmap_set_memrange(m, base, len));
    doca_check(doca_mmap_start(m));
    doca_check(doca_buf_pool_create(n, piece_len, m, &p));
    doca_check(doca_buf_pool_start(p));
    for (auto i = 0uz; i < n; ++i) {
      doca_buf* buf = nullptr;
      doca_check(doca_buf_pool_buf_alloc(p, &buf));
      doca_check(doca_buf_set_data_len(buf, piece_len));
      handles.emplace_back(BorrowedBuffer(buf));
      uint16_t ref_cnt = 0;
      doca_check(doca_buf_get_refcount(buf, &ref_cnt));
      INFO("{} {} {} {}", (void*)handles.back().buf, (void*)handles.back().base, handles.back().len, ref_cnt);
    }
  }

  ~Buffers() {
    TRACE("????? dtor");
    for (auto& h : handles) {
      TRACE("{} {}", (void*)h.buf, (void*)h.base, h.len);
      doca_check(doca_buf_dec_refcount(h.buf, nullptr));
    }
    if (p != nullptr) {
      doca_check(doca_buf_pool_stop(p));
      doca_check(doca_buf_pool_destroy(p));
    }
    if (m != nullptr) {
      doca_check(doca_mmap_stop(m));
      doca_check(doca_mmap_destroy(m));
    }
  }

  Buffers(Buffers&& other) : Base(std::move(other)) {
    m = std::exchange(other.m, nullptr);
    p = std::exchange(other.p, nullptr);
  }

  Buffers& operator=(Buffers&& other) = delete;

 protected:
  doca_mmap* mmap() { return m; }

  doca_mmap* m = nullptr;
  doca_buf_pool* p = nullptr;
};

}  // namespace doca
