#include "provider/tcp/endpoint.hxx"

#include "util/logger.hxx"
#include "util/unreachable.hxx"

namespace dpx::trans::tcp {

Endpoint::Endpoint(size_t queue_depth) {
  if (auto ec = io_uring_queue_init(queue_depth, &ring, 0); ec < 0) {
    die("Fail to init ring, errno: {}", -ec);
  }
}

Endpoint::~Endpoint() { io_uring_queue_exit(&ring); };

bool Endpoint::progress() {
  if (!is_running() && !has_pending_message()) {
    return false;
  }
  io_uring_cqe *cqe = nullptr;
  io_uring_peek_cqe(&ring, &cqe);
  if (cqe == nullptr) {
    return false;
  }
  Context *ctx = reinterpret_cast<Context *>(io_uring_cqe_get_data(cqe));
  if (cqe->res > 0) {
    ctx->tx_size += cqe->res;
  } else {
    ctx->op_res_p.set_value(cqe->res);  // something wrong
    io_uring_cqe_seen(&ring, cqe);
    return true;
  }
  TRACE("tcp done post {} {} size: {}", ctx->op, ctx->mr, ctx->tx_size);
  if (ctx->op == Op::Write && ctx->tx_size < ctx->mr.size()) {  // write partially
    io_uring_cqe_seen(&ring, cqe);
    post(*ctx);
    return true;
  }
  ctx->op_res_p.set_value(ctx->tx_size);
  io_uring_cqe_seen(&ring, cqe);
  return true;
}

op_res_future_t Endpoint::post(Context &ctx) {
  auto f = ctx.op_res_p.get_future();
  if (!is_running()) {
    ERROR("endpoint is stopping...");
    ctx.op_res_p.set_value(to_underlying(ErrorCode::EndpointIsStopping));
  } else {
    _post(ctx);
  }
  return f;
}

// NOTICE
// Because of the tcp stick package problem,
// send the whole buffer in one write/send post
// and wait for all message in one read/recv post
void Endpoint::_post(Context &ctx) {
  auto &buf = ctx.mr;
  DEBUG("tcp post {} {} size: {}", ctx.op, buf, ctx.tx_size);
  auto sqe = io_uring_get_sqe(&ring);
  if (ctx.op == Op::Send || ctx.op == Op::Write) {
    // NOTICE
    // using register buffer api can avoid memory copy,
    // but tcp provider is just used to debug
    // so this feature is reserved for future development.
    // io_uring_prep_write_fixed(sqe, conn, buf.data(), buf.size(), 0, 0);
    io_uring_prep_write(sqe, conn, buf.data() + ctx.tx_size, buf.size() - ctx.tx_size, 0);
  } else if (ctx.op == Op::Recv || ctx.op == Op::Read) {
    io_uring_prep_recv(sqe, conn, buf.data(), buf.size(), MSG_WAITALL);
  } else {
    unreachable();
  }
  io_uring_sqe_set_data(sqe, &ctx);
  if (auto ec = io_uring_submit(&ring); ec < 0) {
    die("Fail to submit sqe, errno: {}", -ec);
  }
}

}  // namespace dpx::trans::tcp