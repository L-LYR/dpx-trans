#include "priv/tcp/endpoint.hxx"

#include "util/fatal.hxx"

namespace tcp {

Endpoint::Endpoint(size_t n_qe, size_t max_payload_size) : buffers(n_qe, max_payload_size), result_ps(n_qe) {
  assert(n_qe != 0 && n_qe % 2 == 0);
  if (auto ec = io_uring_queue_init(n_qe, &ring, 0); ec < 0) {
    die("Fail to init ring, errno: {}", -ec);
  }
  std::vector<iovec> iovecs = buffers.to_iovec();
  if (auto ec = io_uring_register_buffers(&ring, iovecs.data(), iovecs.size()); ec < 0) {
    die("Fail to register buffers, errno: {}", -ec);
  }
}

Endpoint::~Endpoint() {
  if (running()) {
    stop();
    poller.join();
    for (auto &fiber : fibers) {
      fiber.join();
    }
  }
  if (auto ec = io_uring_unregister_buffers(&ring); ec < 0) {
    die("Fail to unregister buffers, errno: {}", -ec);
  }
  io_uring_queue_exit(&ring);
};

size_t Endpoint::poll(size_t n) {
  io_uring_cqe *cqes = nullptr;
  auto got_n = io_uring_peek_batch_cqe(&ring, &cqes, n);
  for (auto &cqe : std::span(cqes, got_n)) {
    auto idx = io_uring_cqe_get_data64(&cqe);
    result_ps[idx].set_value(cqe.res);
    io_uring_cqe_seen(&ring, &cqe);
  }
  return got_n;
}

void Endpoint::run() {
  if (running()) {
    return;
  }
  EndpointBase::run();
  poller = boost::fibers::fiber([this]() {
    while (running()) {
      if (auto n = poll(1); n == 0) {
        boost::this_fiber::sleep_for(100ns);
      }
    }
  });
}

std::tuple<BorrowedBuffer &, size_t, BorrowedBuffer &, size_t> Endpoint::get_buffer_pair() {
  auto in_buf_idx = std::exchange(active_buffer_idx, (active_buffer_idx + 1) % (buffers.size() / 2));
  auto out_buf_idx = in_buf_idx + (buffers.size() / 2);
  buffers[in_buf_idx].clear();
  buffers[out_buf_idx].clear();
  return {buffers[in_buf_idx], in_buf_idx, buffers[out_buf_idx], out_buf_idx};
}

}  // namespace tcp
