#pragma once

#include <liburing.h>

#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"
#include "util/fatal.hxx"
#include "util/unreachable.hxx"

using namespace std::chrono_literals;

namespace tcp {

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;

 public:
  Endpoint() = default;

  ~Endpoint() {
    if (sock != -1) {
      if (auto ec = ::close(sock); ec < 0) {
        die("Fail to close socket {}, errno: {}", sock, errno);
      }
    }
    if (auto ec = io_uring_unregister_buffers(&ring); ec < 0) {
      die("Fail to unregister buffers, errno: {}", -ec);
    }
    io_uring_queue_exit(&ring);
  };

  void prepare(Buffers &buffers) {
    EndpointBase::prepare();
    if (auto ec = io_uring_queue_init(buffers.size(), &ring, 0); ec < 0) {
      die("Fail to init ring, errno: {}", -ec);
    }
    std::vector<iovec> iovecs = buffers.to_iovec();
    if (auto ec = io_uring_register_buffers(&ring, iovecs.data(), iovecs.size()); ec < 0) {
      die("Fail to register buffers, errno: {}", -ec);
    }
    handles.resize(buffers.size());
  }
  void run() { EndpointBase::run(); }
  void stop() { EndpointBase::stop(); }

  size_t progress(size_t n = 1) {
    io_uring_cqe *cqes = nullptr;
    auto got_n = io_uring_peek_batch_cqe(&ring, &cqes, n);
    for (auto &cqe : std::span(cqes, got_n)) {
      auto idx = io_uring_cqe_get_data64(&cqe);
      handles[idx].set_value(cqe.res);
      io_uring_cqe_seen(&ring, &cqe);
    }
    return got_n;
  }

  result_t post_recv(BorrowedBuffer &buf, size_t nbytes) { return post<Op::Read>(buf, nbytes); }

  result_t post_send(BorrowedBuffer &buf, size_t nbytes) { return post<Op::Write>(buf, nbytes); }

 private:
  enum class Op {
    Write,
    Read,
  };

  template <Endpoint::Op op>
  result_t post(BorrowedBuffer &buf, size_t nbytes) {
    assert(running());
    assert(nbytes > 0 && nbytes <= buf.size());
    auto sqe = io_uring_get_sqe(&ring);
    if constexpr (op == Op::Write) {
      io_uring_prep_write_fixed(sqe, sock, buf.data(), nbytes, 0, buf.index());
    } else if constexpr (op == Op::Read) {
      io_uring_prep_read_fixed(sqe, sock, buf.data(), nbytes, 0, buf.index());
    } else {
      static_unreachable;
    }

    // create a new result handle
    handles[next_handle_idx] = result_handle_t{};

    auto &handle = handles[next_handle_idx];
    io_uring_sqe_set_data64(sqe, next_handle_idx);
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit sqe, errno: {}", -ec);
    }

    next_handle_idx = (next_handle_idx + 1) % handles.size();

    return handle.get_future();
  }

  int sock = -1;
  io_uring ring;
  std::vector<result_handle_t> handles;
  uint32_t next_handle_idx = 0;
};

}  // namespace tcp
