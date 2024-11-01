#pragma once

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/verbs/connection.hxx"

namespace verbs {

struct MRHandle {
  uint64_t address = -1;
  size_t length = -1;
  uint32_t rkey = -1;
};

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;

 public:
  Endpoint(Buffers& buffers_);
  ~Endpoint();

  op_res_future_t post_recv(OpContext& ctx, BorrowedBuffer& buffer);

  op_res_future_t post_send(OpContext& ctx, BorrowedBuffer& buffer, uint32_t len);

  bool progress();

  void prepare() { EndpointBase::prepare(); }

  void run() { EndpointBase::run(); }

  void stop() { EndpointBase::stop(); }

 private:
  void setup_resources();
  void setup_remote_param(const rdma_conn_param& remote_);

  EventChannel c;
  rdma_cm_id* id = nullptr;
  Buffers& buffers;
  ibv_cq* cq = nullptr;
  ibv_pd* pd = nullptr;
  ibv_qp* qp = nullptr;
  ibv_mr* mr = nullptr;
  MRHandle local_mr_h;  // used for connection
  MRHandle remote_mr_h;
  rdma_conn_param local = {};
  rdma_conn_param remote = {};
  ibv_device_attr_ex device_attr_ex = {};
};

}  // namespace verbs
