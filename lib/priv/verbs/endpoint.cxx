#include "priv/verbs/endpoint.hxx"

#include "util/fatal.hxx"

namespace verbs {

PD::~PD() {
  if (pd != nullptr) {
    if (auto ec = ibv_dealloc_pd(pd); ec != 0) {
      die("fail to deallocate pd, errno {}", errno);
    }
  }
}

void PD::setup(ibv_context* ctx) {
  if (pd = ibv_alloc_pd(ctx); pd == nullptr) {
    die("Fail to allocate pd, errno {}", errno);
  }
}

MR PD::register_memory(void* base, uint32_t length, int access_flags) {
  auto mr = ibv_reg_mr(pd, base, length, access_flags);
  if (mr == nullptr) {
    die("Fail to register memory region, errno {}", errno);
  }
  return MR(mr);
}

CQ::~CQ() {
  if (cq != nullptr) {
    if (auto rc = ibv_destroy_cq(cq); rc != 0) [[unlikely]] {
      die("Fail to destroy cq, errno{}", errno);
    }
  }
}

void CQ::setup(ibv_context* ctx, uint32_t n_cqe) {
  if (cq = ibv_create_cq(ctx, n_cqe, this, nullptr, 0); cq == nullptr) {
    die("Fail to create cq, errno {}", errno);
  }
}

std::optional<ibv_wc> CQ::poll() {
  ibv_wc wc = {};
  auto ec = ibv_poll_cq(cq, 1, &wc);
  if (ec < 0) {
    die("Fail to poll cq, errno {}", errno);
  }
  return ((ec == 1) ? std::optional<ibv_wc>{wc} : std::nullopt);
}

QP::~QP() {
  if (qp != nullptr) {
    if (auto ec = ibv_destroy_qp(qp); ec != 0) {
      die("Fail to destroy qp, errno {}", errno);
    }
  }
}

void QP::setup(rdma_cm_id* id, PD& pd, CQ& rcq, CQ& scq, const ibv_qp_cap& cap) {
  ibv_qp_init_attr attr = {
      .qp_context = id,
      .send_cq = scq.cq,
      .recv_cq = rcq.cq,
      .srq = nullptr,
      .cap = cap,
      .qp_type = IBV_QPT_RC,
      .sq_sig_all = false,
  };
  if (auto ec = rdma_create_qp(id, pd.pd, &attr); ec != 0) {
    die("Fail to create qp, errno {}", errno);
  }
  qp = id->qp;
}

void QP::post_recv(BorrowedBuffer& buffer, uint32_t lkey, size_t idx) {
  ibv_sge sge = {
      .addr = reinterpret_cast<uint64_t>(buffer.data()),
      .length = static_cast<uint32_t>(buffer.size()),
      .lkey = lkey,
  };
  ibv_recv_wr wr = {
      .wr_id = idx,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
  };
  ibv_recv_wr* bad_wr = nullptr;
  if (auto ec = ibv_post_recv(qp, &wr, &bad_wr); ec < 0) {
    die("Fail to post recv, errno {}", errno);
  }
}

void QP::post_send(BorrowedBuffer& buffer, uint32_t len, uint32_t lkey, size_t idx) {
  ibv_sge sge = {
      .addr = reinterpret_cast<uint64_t>(buffer.data()),
      .length = len,
      .lkey = lkey,
  };
  ibv_send_wr wr = {
      .wr_id = idx,
      .next = nullptr,
      .sg_list = &sge,
      .num_sge = 1,
      .opcode = IBV_WR_SEND,
      .send_flags = IBV_SEND_SIGNALED,
      {},
      {},
      {},
      {},
  };
  ibv_send_wr* bad_wr = nullptr;
  if (auto ec = ibv_post_send(qp, &wr, &bad_wr); ec < 0) {
    die("Fail to post send, errno {}", errno);
  }
}

Endpoint::Endpoint(Buffers& buffers_) : buffers(buffers_) {}

Endpoint::~Endpoint() {
  assert(stopped());
  if (id != nullptr) {
    if (auto ec = rdma_destroy_id(id); ec < 0) {
      die("Fail to destroy cm id, errno: {}", errno);
    }
  }
}

void Endpoint::setup_resources() {
  assert(id != nullptr);
  uint32_t n_wr = buffers.size();
  pd.setup(id->verbs);
  scq.setup(id->verbs, n_wr);
  rcq.setup(id->verbs, n_wr);
  qp.setup(id, pd, rcq, scq,
           ibv_qp_cap{
               .max_send_wr = n_wr,
               .max_recv_wr = n_wr,
               .max_send_sge = 1,
               .max_recv_sge = 1,
               .max_inline_data = 0,
           });
  buffers_mr = pd.register_memory(buffers.base_address(), buffers.total_length());
  local_mr_h.address = reinterpret_cast<uint64_t>(buffers_mr->addr);
  local_mr_h.length = buffers_mr->length;
  local_mr_h.rkey = buffers_mr->rkey;
  ibv_query_device_ex_input query = {};
  if (auto ec = ibv_query_device_ex(id->verbs, &query, &device_attr_ex); ec < 0) {
    die("Fail to query extended attributes of device, errno: {}", errno);
  }
  local.initiator_depth = device_attr_ex.orig_attr.max_qp_init_rd_atom;
  local.responder_resources = device_attr_ex.orig_attr.max_qp_rd_atom;
  local.rnr_retry_count = 7;
  local.private_data = &local_mr_h;
  local.private_data_len = sizeof(local_mr_h);
}

void Endpoint::setup_remote_param(const rdma_conn_param& remote_) {
  remote = remote_;
  if (remote_.private_data != nullptr) {
    remote.private_data = nullptr;  // do not own, just copy out
    remote_mr_h = *reinterpret_cast<const MRHandle*>(remote_.private_data);
  }
}

}  // namespace verbs
