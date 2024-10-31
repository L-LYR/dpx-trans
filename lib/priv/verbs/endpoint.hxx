#pragma once

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/verbs/connection.hxx"
#include "util/fatal.hxx"

namespace verbs {

struct MRHandle {
  uint64_t address = -1;
  size_t length = -1;
  uint32_t rkey = -1;
};

struct MRDeleter {
  void operator()(ibv_mr* p) {
    if (auto ec = ibv_dereg_mr(p); ec != 0) {
      die("Fail to deregiser memory region at (addr: {}, length: {}), ernno {}", p->addr, p->length, errno);
    }
  }
};

using MR = std::unique_ptr<ibv_mr, MRDeleter>;

class PD : Noncopyable, Nonmovable {
  friend class QP;
  friend class Endpoint;

 public:
  PD() = default;
  ~PD();

  void setup(ibv_context* ctx);

  MR register_memory(void* base, uint32_t length,
                     int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);

 private:
  ibv_pd* pd = nullptr;
};

class CQ : Noncopyable, Nonmovable {
  friend class QP;

 public:
  CQ() = default;
  ~CQ();

  void setup(ibv_context* ctx, uint32_t n_cqe);

  std::optional<ibv_wc> poll();

 private:
  ibv_cq* cq;
};

class QP : Noncopyable, Nonmovable {
  friend class Endpoint;

 public:
  QP() = default;
  ~QP();

  void setup(rdma_cm_id* id, PD& pd, CQ& cq, const ibv_qp_cap& cap) { setup(id, pd, cq, cq, cap); }
  void setup(rdma_cm_id* id, PD& pd, CQ& rcq, CQ& scq, const ibv_qp_cap& cap);
  void post_recv(BorrowedBuffer& buffer, uint32_t lkey, size_t idx);
  void post_send(BorrowedBuffer& buffer, uint32_t len, uint32_t lkey, size_t idx);

 private:
  ibv_qp* qp = nullptr;
};

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;

 public:
  Endpoint(Buffers& buffers_);
  ~Endpoint();

  //   template <typename Rpc>
  //   resp_t<Rpc> call(req_t<Rpc>&& req) {
  //     auto [in_buf, in_buf_idx, out_buf, out_buf_idx] = get_buffer_pair();
  //     auto serializer = zpp::bits::out(in_buf);
  //     serializer(req).or_throw();
  //     std::cout << std::endl << Hexdump(in_buf.data(), serializer.position()) << std::endl;
  //     INFO("Write {} bytes", serializer.position());
  //     qp.post_send(in_buf, serializer.position(), buffers_mr.lkey(), in_buf_idx);
  //     while (true) {
  //       if (auto o = cq.poll(); o.has_value()) {
  //         assert(o->status == IBV_WC_SUCCESS);
  //         assert(o->opcode == IBV_WC_SEND);
  //         assert(o->wr_id == in_buf_idx);
  //         break;
  //       }
  //     }
  //     qp.post_recv(out_buf, buffers_mr.lkey(), out_buf_idx);
  //     while (true) {
  //       if (auto o = cq.poll(); o.has_value()) {
  //         assert(o->status == IBV_WC_SUCCESS);
  //         assert(o->opcode == IBV_WC_RECV);
  //         assert(o->wr_id == out_buf_idx);
  //         break;
  //       }
  //     }
  //     auto resp = resp_t<Rpc>{};
  //     auto deserializer = zpp::bits::in(out_buf);
  //     deserializer(resp).or_throw();
  //     std::cout << std::endl << Hexdump(out_buf.data(), deserializer.position()) << std::endl;
  //     INFO("Read {} bytes", deserializer.position());
  //     return resp;
  //   }

  //   template <typename Rpc>
  //   void serve(handler_t<Rpc>&& fn) {
  //     auto [in, in_buf_idx, out, out_buf_idx] = get_buffer_pair();
  //     qp.post_recv(out, buffers_mr.lkey(), out_buf_idx);
  //     while (true) {
  //       if (auto o = cq.poll(); o.has_value()) {
  //         assert(o->status == IBV_WC_SUCCESS);
  //         assert(o->opcode == IBV_WC_RECV);
  //         assert(o->wr_id == out_buf_idx);
  //         break;
  //       }
  //     }
  //     auto req = req_t<Rpc>{};
  //     auto deserializer = zpp::bits::in(out);
  //     deserializer(req).or_throw();
  //     std::cout << std::endl << Hexdump(out.data(), deserializer.position()) << std::endl;
  //     INFO("Read {} bytes", deserializer.position());
  //     auto resp = fn(std::move(req));
  //     auto serializer = zpp::bits::out(in);
  //     serializer(resp).or_throw();
  //     std::cout << std::endl << Hexdump(in.data(), serializer.position()) << std::endl;
  //     INFO("Write {} bytes", serializer.position());
  //     qp.post_send(in, serializer.position(), buffers_mr.lkey(), in_buf_idx);
  //     while (true) {
  //       if (auto o = cq.poll(); o.has_value()) {
  //         assert(o->status == IBV_WC_SUCCESS);
  //         assert(o->opcode == IBV_WC_SEND);
  //         assert(o->wr_id == in_buf_idx);
  //         break;
  //       }
  //     }
  //   }

  void prepare() { EndpointBase::prepare(); }
  void run() { EndpointBase::run(); }
  void stop() {
    if (auto ec = rdma_disconnect(id); ec < 0) {
      die("Fail to disconnect, errno: {}", errno);
    }
    c.wait_and_ack(RDMA_CM_EVENT_DISCONNECTED);
    EndpointBase::stop();
  }

 private:
  void setup_resources();
  void setup_remote_param(const rdma_conn_param& remote_);

  EventChannel c;
  rdma_cm_id* id = nullptr;
  Buffers& buffers;
  CQ scq;
  CQ rcq;
  PD pd;
  QP qp;
  MR buffers_mr;
  MRHandle local_mr_h;  // used for connection
  MRHandle remote_mr_h;
  rdma_conn_param local = {};
  rdma_conn_param remote = {};
  ibv_device_attr_ex device_attr_ex = {};
};

}  // namespace verbs
