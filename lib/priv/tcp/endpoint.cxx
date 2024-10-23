#include "priv/tcp/endpoint.hxx"

#include "util/fatal.hxx"

namespace tcp {

Endpoint::Endpoint(size_t n_qe, size_t max_payload_size) : buffers(n_qe, max_payload_size), result_ps(n_qe) {
  assert(n_qe != 0 && n_qe % 2 == 0);
  if (auto ec = io_uring_queue_init(n_qe, &ring, 0); ec < 0) {
    die("Fail to init ring, errno: {}", -ec);
  }
  std::vector<iovec> iovecs = buffers;
  if (auto ec = io_uring_register_buffers(&ring, iovecs.data(), buffers.size()); ec < 0) {
    die("Fail to register buffers, errno: {}", -ec);
  }
}

Endpoint::~Endpoint() {
  if (auto ec = io_uring_unregister_buffers(&ring); ec < 0) {
    die("Fail to unregister buffers, errno: {}", -ec);
  }
  io_uring_queue_exit(&ring);
};

Buffer &Endpoint::get_send_buffer() {
  active_send_buffer_idx = (active_send_buffer_idx + 1) % (buffers.size() / 2);
  buffers[active_send_buffer_idx].clear();
  return buffers[active_send_buffer_idx];
}

Buffer &Endpoint::get_recv_buffer() {
  active_recv_buffer_idx = (active_recv_buffer_idx + 1) % (buffers.size() / 2);
  buffers[active_recv_buffer_idx + buffers.size() / 2].clear();
  return buffers[active_recv_buffer_idx + buffers.size() / 2];
}

}  // namespace tcp
