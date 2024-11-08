#include "priv/verbs/endpoint.hxx"

#include <glaze/glaze.hpp>

#include "util/fatal.hxx"

namespace verbs {

bool Endpoint::progress() {
  ibv_wc wc = {};
  auto ec = ibv_poll_cq(cq, 1, &wc);
  if (ec < 0) {
    die("Fail to poll cq, errno {}", errno);
  }
  if (ec == 1) {
    auto ctx = reinterpret_cast<OpContext*>(wc.wr_id);
    if (wc.status == IBV_WC_WR_FLUSH_ERR) {  // diconnected, notify workers to stop
      ctx->op_res.set_value(0);
    } else if (wc.status == IBV_WC_SUCCESS) {
      switch (wc.opcode) {
        case IBV_WC_RECV:
        case IBV_WC_RECV_RDMA_WITH_IMM: {
          ctx->op_res.set_value(wc.imm_data);
        } break;
        case IBV_WC_SEND: {
          ctx->op_res.set_value(ctx->len);
        } break;
        default: {
          die("Unexpected wc opcode {}", static_cast<int>(wc.opcode));
        }
      }
    } else {
      die("Error wc, status: {}", ibv_wc_status_str(wc.status));
    }
    return true;
  }
  return false;
}

op_res_future_t Endpoint::post_recv(OpContext& ctx) {
  ibv_sge sge = {
      .addr = reinterpret_cast<uint64_t>(ctx.buf.data()),
      .length = static_cast<uint32_t>(ctx.len),
      .lkey = recv_mr->lkey,
  };
  ibv_recv_wr wr = {
      .wr_id = reinterpret_cast<uint64_t>(&ctx),
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
  };
  ibv_recv_wr* bad_wr = nullptr;
  if (auto ec = ibv_post_recv(qp, &wr, &bad_wr); ec < 0) {
    die("Fail to post recv, errno {}", errno);
  }
  return ctx.op_res.get_future();
}

op_res_future_t Endpoint::post_send(OpContext& ctx) {
  ibv_sge sge = {
      .addr = reinterpret_cast<uint64_t>(ctx.buf.data()),
      .length = static_cast<uint32_t>(ctx.len),
      .lkey = send_mr->lkey,
  };
  ibv_send_wr wr = {
      .wr_id = reinterpret_cast<uint64_t>(&ctx),
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_SEND_WITH_IMM,
      .send_flags = IBV_SEND_SIGNALED,
      {
          .imm_data = static_cast<uint32_t>(ctx.len),
      },
      {},
      {},
      {},
  };
  ibv_send_wr* bad_wr = nullptr;
  if (auto ec = ibv_post_send(qp, &wr, &bad_wr); ec < 0) {
    die("Fail to post send, errno {}", errno);
  }
  return ctx.op_res.get_future();
}

Endpoint::Endpoint(naive::Buffers& send_buffers_, naive::Buffers& recv_buffers_)
    : send_buffers(send_buffers_), recv_buffers(recv_buffers_) {}

Endpoint::~Endpoint() {
  if (qp != nullptr) {
    if (auto ec = ibv_destroy_qp(qp); ec != 0) {
      die("Fail to destroy qp, errno {}", errno);
    }
  }
  if (id != nullptr) {
    if (auto ec = rdma_destroy_id(id); ec < 0) {
      die("Fail to destroy cm id, errno: {}", errno);
    }
  }
  if (cq != nullptr) {
    if (auto rc = ibv_destroy_cq(cq); rc != 0) {
      die("Fail to destroy cq, errno{}", errno);
    }
  }
  if (send_mr != nullptr) {
    if (auto ec = ibv_dereg_mr(send_mr); ec != 0) {
      die("Fail to deregiser memory region at (addr: {}, length: {}), ernno {}", send_mr->addr, send_mr->length, errno);
    }
  }
  if (recv_mr != nullptr) {
    if (auto ec = ibv_dereg_mr(recv_mr); ec != 0) {
      die("Fail to deregiser memory region at (addr: {}, length: {}), ernno {}", recv_mr->addr, recv_mr->length, errno);
    }
  }
  if (pd != nullptr) {
    if (auto ec = ibv_dealloc_pd(pd); ec != 0) {
      die("fail to deallocate pd, errno {}", errno);
    }
  }
}

void Endpoint::prepare() {
  assert(id != nullptr);
  uint32_t n_send_wr = send_buffers.n_elements();
  uint32_t n_recv_wr = recv_buffers.n_elements();
  if (pd = ibv_alloc_pd(id->verbs); pd == nullptr) {
    die("Fail to allocate pd, errno {}", errno);
  }
  if (cq = ibv_create_cq(id->verbs, n_send_wr + n_recv_wr, this, nullptr, 0); cq == nullptr) {
    die("Fail to create cq, errno {}", errno);
  }
  ibv_qp_init_attr attr = {
      .qp_context = this,
      .send_cq = cq,
      .recv_cq = cq,
      .srq = nullptr,
      .cap =
          ibv_qp_cap{
              .max_send_wr = n_send_wr,
              .max_recv_wr = n_recv_wr,
              .max_send_sge = 1,
              .max_recv_sge = 1,
              .max_inline_data = 0,
          },
      .qp_type = IBV_QPT_RC,
      .sq_sig_all = false,
  };
  if (auto ec = rdma_create_qp(id, pd, &attr); ec != 0) {
    die("Fail to create qp, errno {}", errno);
  }
  qp = id->qp;
  if (send_mr = ibv_reg_mr(pd, send_buffers.data(), send_buffers.size(),
                           IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
      send_mr == nullptr) {
    die("Fail to register memory region, errno {}", errno);
  }
  local_mr_h[0].address = reinterpret_cast<uint64_t>(send_mr->addr);
  local_mr_h[0].length = send_mr->length;
  local_mr_h[0].rkey = send_mr->rkey;
  if (recv_mr = ibv_reg_mr(pd, recv_buffers.data(), recv_buffers.size(),
                           IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
      recv_mr == nullptr) {
    die("Fail to register memory region, errno {}", errno);
  }
  local_mr_h[1].address = reinterpret_cast<uint64_t>(recv_mr->addr);
  local_mr_h[1].length = recv_mr->length;
  local_mr_h[1].rkey = recv_mr->rkey;
  ibv_device_attr_ex device_attr_ex = {};
  ibv_query_device_ex_input query = {};
  if (auto ec = ibv_query_device_ex(id->verbs, &query, &device_attr_ex); ec < 0) {
    die("Fail to query extended attributes of device, errno: {}", errno);
  }
  INFO("max_qp_init_rd_atom: {} max_qp_rd_atom: {}", device_attr_ex.orig_attr.max_qp_init_rd_atom,
       device_attr_ex.orig_attr.max_qp_rd_atom);
  local.initiator_depth = device_attr_ex.orig_attr.max_qp_init_rd_atom;
  local.responder_resources = device_attr_ex.orig_attr.max_qp_rd_atom;
  local.rnr_retry_count = 7;
  local.private_data = &local_mr_h;
  local.private_data_len = sizeof(local_mr_h);
  EndpointBase::prepare();
}

void Endpoint::setup_remote_param(const rdma_conn_param& remote_) {
  remote = remote_;
  if (remote_.private_data != nullptr) {
    remote.private_data = nullptr;  // do not own, just copy out
    remote_mr_h = *reinterpret_cast<const MRHandle*>(remote_.private_data);
  }
}

}  // namespace verbs
