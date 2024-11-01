#pragma once

#include <liburing.h>

#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"

using namespace std::chrono_literals;

namespace tcp {

class Endpoint : public EndpointBase {
  friend class ConnectionHandle;

 public:
  Endpoint(Buffers &buffers_);

  ~Endpoint();

  void prepare();
  void run();
  void stop();

  bool progress();

  op_res_future_t post_recv(OpContext &ctx, BorrowedBuffer &buf);
  op_res_future_t post_send(OpContext &ctx, BorrowedBuffer &buf, [[maybe_unused]] size_t len);

 private:
  template <Op op>
  op_res_future_t post(OpContext &ctx, BorrowedBuffer &buf);

  int sock = -1;  // do not own, just borrowed
  io_uring ring;
  Buffers &buffers;
};

}  // namespace tcp
