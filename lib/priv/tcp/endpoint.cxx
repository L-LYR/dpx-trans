#include "priv/tcp/endpoint.hxx"

#include "util/fatal.hxx"
#include "util/unreachable.hxx"

namespace tcp {

Endpoint::Endpoint(naive::Buffers &buffers_) : buffers(buffers_) {
  if (auto ec = io_uring_queue_init(buffers.n_elements(), &ring, 0); ec < 0) {
    die("Fail to init ring, errno: {}", -ec);
  }
  iovec v = {.iov_base = buffers.data(), .iov_len = buffers.size()};
  if (auto ec = io_uring_register_buffers(&ring, &v, 1); ec < 0) {
    die("Fail to register buffers, errno: {}", -ec);
  }
  EndpointBase::prepare();
}

Endpoint::~Endpoint() {
  if (auto ec = io_uring_unregister_buffers(&ring); ec < 0) {
    die("Fail to unregister buffers, errno: {}", -ec);
  }
  io_uring_queue_exit(&ring);
};

bool Endpoint::progress() {
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

op_res_future_t Endpoint::post_recv(OpContext &ctx) { return post<Op::Recv>(ctx); }

op_res_future_t Endpoint::post_send(OpContext &ctx) { return post<Op::Send>(ctx); }

// NOTICE
// Because of the tcp stick package problem, we here send the whole buffer in one post.
template <Op op>
op_res_future_t Endpoint::post(OpContext &ctx) {
  auto &buf = ctx.buf;
  auto sqe = io_uring_get_sqe(&ring);
  if constexpr (op == Op::Send) {
    DEBUG("{} {}", (void *)buf.data(), buf.size());
    io_uring_prep_write_fixed(sqe, sock, buf.data(), buf.size(), 0, 0);
  } else if constexpr (op == Op::Recv) {
    DEBUG("{} {}", (void *)buf.data(), buf.size());
    io_uring_prep_read_fixed(sqe, sock, buf.data(), buf.size(), 0, 0);
  } else {
    static_unreachable;
  }
  io_uring_sqe_set_data(sqe, &ctx);
  if (auto ec = io_uring_submit(&ring); ec < 0) {
    die("Fail to submit sqe, errno: {}", -ec);
  }
  return ctx.op_res.get_future();
}

}  // namespace tcp
