#pragma once

#include <doca_buf.h>
#include <doca_buf_pool.h>
#include <doca_mmap.h>

#include "memory/simple_buffer.hxx"
#include "util/doca_wrapper_def.hxx"

class MmapBuffers;

namespace doca_wrapper {

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

inline std::pair<uint8_t*, size_t> doca_buf_data(doca_buf* buf) {
  void* data = nullptr;
  size_t data_len = 0;
  doca_check(doca_buf_get_data(buf, &data));
  doca_check(doca_buf_get_data_len(buf, &data_len));
  return {reinterpret_cast<uint8_t*>(data), data_len};
}

inline std::pair<uint8_t*, size_t> doca_buf_head(doca_buf* buf) {
  void* data = nullptr;
  void* head = nullptr;
  doca_check(doca_buf_get_data(buf, &data));
  doca_check(doca_buf_get_data(buf, &head));
  return {reinterpret_cast<uint8_t*>(head), reinterpret_cast<uint8_t*>(data) - reinterpret_cast<uint8_t*>(head)};
}

DocaComchConsumer create_comch_consumer(ComchConnection connection, MmapBuffers& buffers);

}  // namespace doca_wrapper

class MmapBuffer : public BorrowedBuffer {
 public:
  MmapBuffer(doca_buf* buf_) : BorrowedBuffer(doca_wrapper::doca_buf_data(buf_)), buf(buf_) {
    std::tie(head, head_len) = doca_wrapper::doca_buf_head(buf);
  }
  ~MmapBuffer() {
    if (buf != nullptr) {
      doca_check(doca_buf_dec_refcount(buf, nullptr));
    }
  }

  MmapBuffer(MmapBuffer&& other) : BorrowedBuffer(std::move(other)) { buf = std::exchange(other.buf, nullptr); }

  MmapBuffer& operator=(MmapBuffer&& other) = delete;

 private:
  uint8_t* head = nullptr;
  size_t head_len = -1;
  doca_buf* buf = nullptr;
};

class MmapBuffers : Noncopyable {
 public:
  MmapBuffers(DocaDev& dev, size_t n, size_t piece_len_)
      : total_len(n * piece_len_), piece_len(piece_len_), base(new uint8_t[total_len]) {
    doca_check(doca_mmap_create(&mmap));
    doca_check(doca_mmap_add_dev(mmap, dev.get()));
    doca_check(doca_mmap_set_permissions(mmap, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
    doca_check(doca_mmap_set_memrange(mmap, base, total_len));
    doca_check(doca_mmap_start(mmap));
    doca_check(doca_buf_pool_create(n, piece_len_, mmap, &pool));
    doca_check(doca_buf_pool_start(pool));
  }
  ~MmapBuffers() {
    if (base != nullptr) {
      doca_check(doca_buf_pool_stop(pool));
      doca_check(doca_buf_pool_destroy(pool));
      doca_check(doca_mmap_stop(mmap));
      doca_check(doca_mmap_destroy(mmap));
      delete[] base;
    }
  };

  MmapBuffers(MmapBuffers&& other) {
    total_len = std::exchange(other.total_len, -1);
    piece_len = std::exchange(other.piece_len, -1);
    base = std::exchange(other.base, nullptr);
    mmap = std::exchange(other.mmap, nullptr);
    pool = std::exchange(other.pool, nullptr);
  }

  MmapBuffers& operator=(MmapBuffers&& other) = delete;

  uint8_t* base_address() { return base; }
  const uint8_t* base_address() const { return base; }
  size_t total_length() const { return total_len; }
  size_t piece_length() const { return piece_len; }
  size_t size() const {
    uint32_t n_elements = 0;
    doca_check(doca_buf_pool_get_num_elements(pool, &n_elements));
    return n_elements;
  }

 private:
  doca_mmap* underlying() { return mmap; }

  friend DocaComchConsumer doca_wrapper::create_comch_consumer(ComchConnection, MmapBuffers&);

  size_t total_len = -1;
  size_t piece_len = -1;
  uint8_t* base = nullptr;
  doca_mmap* mmap = nullptr;
  doca_buf_pool* pool = nullptr;
};
