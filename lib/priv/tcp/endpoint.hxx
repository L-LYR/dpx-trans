#pragma once

#include <liburing.h>

#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"

namespace tcp {

class Endpoint : public EndpointBase {
  friend class ConnectionHandle;

 public:
  Endpoint(naive::Buffers &buffers_);

  ~Endpoint();

  bool progress();

  op_res_future_t post_recv(OpContext &ctx);
  op_res_future_t post_send(OpContext &ctx);

 private:
  template <Op op>
  op_res_future_t post(OpContext &ctx);

  int sock = -1;  // do not own, just borrowed
  io_uring ring;
  naive::Buffers &buffers;
};

}  // namespace tcp
