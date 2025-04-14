#pragma once

#include <liburing.h>

#include "context.hxx"

namespace dpx::trans::tcp {

class ConnHolder;

class Endpoint {
  friend class ConnHolder;

 public:
  explicit Endpoint(size_t queue_depth);
  ~Endpoint();

  bool is_running() { return running.load(std::memory_order_relaxed); }
  bool has_pending_message() { return outstanding_sqe.load(std::memory_order_relaxed) > 0; }
  bool progress();

  op_res_future_t post(Context& ctx);

 protected:
  void start() { running = true; }
  void stop() { running = false; }

  void _post(Context& ctx);

 private:
  std::atomic_bool running = false;
  io_uring ring;
  size_t queue_depth = 0;
  std::atomic_size_t outstanding_sqe = 0;
  int conn = -1;
};

}  // namespace dpx::trans::tcp
