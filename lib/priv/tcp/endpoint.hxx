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
    iovec v = {
        .iov_base = buffers.base_address(),
        .iov_len = buffers.total_length(),
    };
    if (auto ec = io_uring_register_buffers(&ring, &v, 1); ec < 0) {
      die("Fail to register buffers, errno: {}", -ec);
    }
  }
  void run() { EndpointBase::run(); }
  void stop() { EndpointBase::stop(); }

  bool progress() {
    io_uring_cqe *cqe = nullptr;
    io_uring_peek_cqe(&ring, &cqe);
    if (cqe != nullptr) {
      OpContext *ctx = reinterpret_cast<OpContext *>(io_uring_cqe_get_data(cqe));
      ctx->op_res.set_value(cqe->res);
      io_uring_cqe_seen(&ring, cqe);
      return true;
    }
    return false;
  }

  op_res_future_t post_recv(OpContext &ctx, BorrowedBuffer &buf, size_t nbytes) {
    return post<Op::Recv>(ctx, buf, nbytes);
  }

  op_res_future_t post_send(OpContext &ctx, BorrowedBuffer &buf, size_t nbytes) {
    return post<Op::Send>(ctx, buf, nbytes);
  }

 private:
  template <Op op>
  op_res_future_t post(OpContext &ctx, BorrowedBuffer &buf, size_t nbytes) {
    assert(running());
    assert(nbytes > 0 && nbytes <= buf.size());
    auto sqe = io_uring_get_sqe(&ring);
    if constexpr (op == Op::Send) {
      io_uring_prep_write_fixed(sqe, sock, buf.data(), nbytes, 0, 0);
    } else if constexpr (op == Op::Recv) {
      io_uring_prep_read_fixed(sqe, sock, buf.data(), nbytes, 0, 0);
    } else {
      static_unreachable;
    }
    io_uring_sqe_set_data(sqe, &ctx);
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit sqe, errno: {}", -ec);
    }
    return ctx.op_res.get_future();
  }

  int sock = -1;
  io_uring ring;
};

}  // namespace tcp
