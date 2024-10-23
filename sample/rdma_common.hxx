#pragma once

#include <arpa/inet.h>
#include <rdma/rdma_cma.h>
#include <spdlog/spdlog.h>
#include <zpp_bits.h>

#include <iostream>

#include "common.hxx"
#include "memory/simple_buffer.hxx"
#include "util/fatal.hxx"
#include "util/hex_dump.hxx"
#include "util/logger.hxx"

class EventChannel : Noncopyable, Nonmovable {
  friend class Connection;
  friend class ConnectionHandle;

 public:
  explicit EventChannel(rdma_event_channel* p_) : own(false), p(p_) {}
  EventChannel() : own(true) {
    if (p = rdma_create_event_channel(); p == nullptr) {
      die("Fail to create event channel, errno: {}", errno);
    }
  }
  ~EventChannel() {
    if (own && p != nullptr) {
      rdma_destroy_event_channel(p);
    }
  }

  [[nodiscard("Must not ignore the cm event")]] rdma_cm_event* wait(rdma_cm_event_type expected) {
    rdma_cm_event* e = nullptr;
    if (auto ec = rdma_get_cm_event(p, &e); ec < 0) {
      die("Fail to get expected event {}, errno: {}", rdma_event_str(expected), errno);
    } else if (e->status != 0) {
      die("Get a bad event {}, status: {}, expect {}", rdma_event_str(e->event), e->status, rdma_event_str(expected));
    } else if (e->event != expected) {
      die("Expect event {}, but get event {}", rdma_event_str(expected), rdma_event_str(e->event));
    }
    return e;
  }

  void ack(rdma_cm_event* e) {
    assert(e != nullptr);
    if (auto ec = rdma_ack_cm_event(e); ec < 0) {
      die("Fail to ack cm event {}, errno: {}", rdma_event_str(e->event), errno);
    }
  }

  void wait_and_ack(rdma_cm_event_type expected) { ack(wait(expected)); }

 private:
  rdma_event_channel* underlying() { return p; }

  bool own = false;
  rdma_event_channel* p = nullptr;
};

class MRHandle {
 public:
  MRHandle() = default;
  MRHandle(void* addr_, uint32_t len_, uint32_t remote_key_)
      : addr(reinterpret_cast<uint64_t>(addr_)), len(len_), remote_key(remote_key_) {}
  ~MRHandle() = default;

  uint64_t address() { return addr; }
  uint32_t length() { return len; }
  uint32_t rkey() { return remote_key; }

 private:
  uint64_t addr = -1;
  uint32_t len = -1;
  uint32_t remote_key = -1;
};

class PD : Noncopyable, Nonmovable {
  friend class QP;
  friend class Endpoint;

  class MR : Noncopyable {
    friend class PD;

   public:
    MR() = default;
    ~MR() { deregister(); }

    MR(MR&& other) { mr = std::exchange(other.mr, nullptr); }
    MR& operator=(MR&& other) {
      if (this != &other) {
        deregister();
        mr = std::exchange(other.mr, nullptr);
      }
      return *this;
    }

    operator MRHandle() const { return MRHandle(mr->addr, mr->length, mr->rkey); }

    uint64_t address() const { return reinterpret_cast<uint64_t>(mr->addr); }
    uint32_t length() const { return mr->length; }
    uint32_t rkey() const { return mr->rkey; }
    uint32_t lkey() const { return mr->lkey; }

   private:
    MR(ibv_mr* mr_) : mr(mr_) {}

    void deregister() {
      if (mr != nullptr) {
        if (auto ec = ibv_dereg_mr(mr); ec != 0) {
          die("Fail to deregiser memory region at (addr: {}, length: {}), ernno {}", mr->addr, mr->length, errno);
        }
      }
    }

    ibv_mr* mr = nullptr;
  };

 public:
  PD() = default;
  ~PD() {
    if (pd != nullptr) {
      if (auto ec = ibv_dealloc_pd(pd); ec != 0) {
        die("fail to deallocate pd, errno {}", errno);
      }
    }
  }

  void setup(ibv_context* ctx) {
    if (pd = ibv_alloc_pd(ctx); pd == nullptr) {
      die("Fail to allocate pd, errno {}", errno);
    }
  }

  MR register_memory(void* base, uint32_t length,
                     int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE) {
    auto mr = ibv_reg_mr(pd, base, length, access_flags);
    if (mr == nullptr) {
      die("Fail to register memory region, errno {}", errno);
    }
    return MR(mr);
  }

 private:
  ibv_pd* pd = nullptr;
};

class CQ : Noncopyable, Nonmovable {
  friend class QP;

 public:
  CQ() = default;
  ~CQ() {
    if (cq != nullptr) {
      if (auto rc = ibv_destroy_cq(cq); rc != 0) [[unlikely]] {
        die("Fail to destroy cq, errno{}", errno);
      }
    }
  }

  void setup(ibv_context* ctx, uint32_t n_cqe) {
    if (cq = ibv_create_cq(ctx, n_cqe, this, nullptr, 0); cq == nullptr) {
      die("Fail to create cq, errno {}", errno);
    }
  }

  std::optional<ibv_wc> poll() {
    ibv_wc wc = {};
    auto ec = ibv_poll_cq(cq, 1, &wc);
    if (ec < 0) {
      die("Fail to poll cq, errno {}", errno);
    }
    return ((ec == 1) ? std::optional<ibv_wc>{wc} : std::nullopt);
  }

 private:
  ibv_cq* cq;
};

class QP : Noncopyable, Nonmovable {
  friend class Endpoint;

 public:
  QP() = default;
  ~QP() {
    if (qp != nullptr) {
      if (auto ec = ibv_destroy_qp(qp); ec != 0) {
        die("Fail to destroy qp, errno {}", errno);
      }
    }
  }

  void setup(rdma_cm_id* id, PD& pd, CQ& cq, const ibv_qp_cap& cap) { setup(id, pd, cq, cq, cap); }

  void setup(rdma_cm_id* id, PD& pd, CQ& rcq, CQ& scq, const ibv_qp_cap& cap) {
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

  void post_recv(Buffer& buffer, uint32_t lkey) {
    ibv_sge sge = {
        .addr = reinterpret_cast<uint64_t>(buffer.data()),
        .length = static_cast<uint32_t>(buffer.size()),
        .lkey = lkey,
    };
    ibv_recv_wr wr = {
        .wr_id = buffer.index(),
        .next = nullptr,
        .sg_list = &sge,
        .num_sge = 1,
    };
    ibv_recv_wr* bad_wr = nullptr;
    if (auto ec = ibv_post_recv(qp, &wr, &bad_wr); ec < 0) {
      die("Fail to post recv, errno {}", errno);
    }
  }

  void post_send(Buffer& buffer, uint32_t len, uint32_t lkey) {
    ibv_sge sge = {
        .addr = reinterpret_cast<uint64_t>(buffer.data()),
        .length = len,
        .lkey = lkey,
    };
    ibv_send_wr wr = {
        .wr_id = buffer.index(),
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

 private:
  ibv_qp* qp = nullptr;
};

class Connection : public ConnectionBase {
  friend class Acceptor;
  friend class Connector;
  friend class Endpoint;

 public:
  ~Connection() {
    if (side == Side::ClientSide) {
      if (auto ec = rdma_disconnect(id); ec < 0) {
        die("Fail to disconnect with {}:{}, errno: {}", remote_ip, remote_port, errno);
      }
    }
    c.wait_and_ack(RDMA_CM_EVENT_DISCONNECTED);
    if (id != nullptr) {
      if (auto ec = rdma_destroy_id(id); ec < 0) {
        die("Fail to destroy cm id, errno: {}", errno);
      }
    }
  }

 private:
  static ConnectionPtr establish(Side side, rdma_cm_id* id, Endpoint& endpoint);

  Connection(Side side_, rdma_cm_id* id_) : ConnectionBase(side_), id(id_) {
    if (auto ec = rdma_migrate_id(id, c.underlying()); ec < 0) {
      die("Fail to migrate cm id to new event channel, errno: {}", errno);
    }

    auto local_addr = reinterpret_cast<sockaddr_in*>(rdma_get_local_addr(id));
    local_ip = inet_ntoa(local_addr->sin_addr);
    local_port = ntohs(local_addr->sin_port);
    auto remote_addr = reinterpret_cast<sockaddr_in*>(rdma_get_peer_addr(id));
    remote_ip = inet_ntoa(remote_addr->sin_addr);
    remote_port = ntohs(remote_addr->sin_port);
  }

 private:
  rdma_cm_id* id = nullptr;
  EventChannel c;  // do migrate after establish, for disconnection
};

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;

 public:
  Endpoint(size_t n_qe, size_t max_payload_size)
      : send_buffers(n_qe, max_payload_size), recv_buffers(n_qe, max_payload_size) {}
  ~Endpoint() = default;

  template <typename Rpc>
  resp_t<Rpc> call(req_t<Rpc>&& req) {
    auto& in = get_send_buffer();
    auto serializer = zpp::bits::out(in);
    serializer(req).or_throw();
    std::cout << std::endl << Hexdump(in.data(), serializer.position()) << std::endl;
    INFO("Write {} bytes", serializer.position());

    qp.post_send(in, serializer.position(), local_send_mr.lkey());
    while (true) {
      if (auto o = cq.poll(); o.has_value()) {
        assert(o->status == IBV_WC_SUCCESS);
        assert(o->opcode == IBV_WC_SEND);
        assert(o->wr_id == in.index());
        break;
      }
    }

    auto& out = get_recv_buffer();
    qp.post_recv(out, local_recv_mr.lkey());
    while (true) {
      if (auto o = cq.poll(); o.has_value()) {
        assert(o->status == IBV_WC_SUCCESS);
        assert(o->opcode == IBV_WC_RECV);
        assert(o->wr_id == out.index());
        break;
      }
    }

    auto resp = resp_t<Rpc>{};
    auto deserializer = zpp::bits::in(out);
    deserializer(resp).or_throw();
    std::cout << std::endl << Hexdump(out.data(), deserializer.position()) << std::endl;
    INFO("Read {} bytes", deserializer.position());

    return resp;
  }

  template <typename Rpc>
  void serve(handler_t<Rpc>&& fn) {
    auto& out = get_recv_buffer();
    qp.post_recv(out, local_recv_mr.lkey());

    while (true) {
      if (auto o = cq.poll(); o.has_value()) {
        assert(o->status == IBV_WC_SUCCESS);
        assert(o->opcode == IBV_WC_RECV);
        assert(o->wr_id == out.index());
        break;
      }
    }

    auto req = req_t<Rpc>{};
    auto deserializer = zpp::bits::in(out);
    deserializer(req).or_throw();
    std::cout << std::endl << Hexdump(out.data(), deserializer.position()) << std::endl;
    INFO("Read {} bytes", deserializer.position());

    auto resp = fn(std::move(req));

    auto& in = get_send_buffer();
    auto serializer = zpp::bits::out(in);
    serializer(resp).or_throw();

    std::cout << std::endl << Hexdump(in.data(), serializer.position()) << std::endl;
    INFO("Write {} bytes", serializer.position());

    qp.post_send(in, serializer.position(), local_send_mr.lkey());
    while (true) {
      if (auto o = cq.poll(); o.has_value()) {
        assert(o->status == IBV_WC_SUCCESS);
        assert(o->opcode == IBV_WC_SEND);
        assert(o->wr_id == in.index());
        break;
      }
    }
  }

 private:
  void setup_remote_param(const rdma_conn_param& remote_) {
    remote = remote_;
    if (remote_.private_data != nullptr) {
      remote.private_data = nullptr;  // do not own, just copy out
      // remote_mr_h = *reinterpret_cast<const MRHandle*>(remote_.private_data);
    }
  }

  void setup_resources(rdma_cm_id* id) {
    uint32_t n_wr = send_buffers.size();
    pd.setup(id->verbs);
    cq.setup(id->verbs, n_wr * 2);
    qp.setup(id, pd, cq,
             ibv_qp_cap{
                 .max_send_wr = n_wr,
                 .max_recv_wr = n_wr,
                 .max_send_sge = 1,
                 .max_recv_sge = 1,
                 .max_inline_data = 0,
             });
    local_send_mr = pd.register_memory(send_buffers.base_address(), send_buffers.length());
    local_recv_mr = pd.register_memory(recv_buffers.base_address(), recv_buffers.length());
    // local_mr_h = local_mr;
    // setup connection params
    ibv_query_device_ex_input query = {};
    if (auto ec = ibv_query_device_ex(id->verbs, &query, &device_attr_ex); ec < 0) {
      die("Fail to query extended attributes of device, errno: {}", errno);
    }
    local.initiator_depth = device_attr_ex.orig_attr.max_qp_init_rd_atom;
    local.responder_resources = device_attr_ex.orig_attr.max_qp_rd_atom;
    local.rnr_retry_count = 7;
    // local.private_data = &local_mr_h;
    // local.private_data_len = sizeof(local_mr_h);
  }

  // TODO better one
  Buffer& get_send_buffer() {
    active_send_buffer_idx = (active_send_buffer_idx + 1) % send_buffers.size();
    send_buffers[active_send_buffer_idx].clear();
    return send_buffers[active_send_buffer_idx];
  }

  Buffer& get_recv_buffer() {
    active_recv_buffer_idx = (active_recv_buffer_idx + 1) % recv_buffers.size();
    recv_buffers[active_recv_buffer_idx].clear();
    return recv_buffers[active_recv_buffer_idx];
  }

  Buffers send_buffers;
  Buffers recv_buffers;

  CQ cq;
  PD pd;
  QP qp;
  PD::MR local_send_mr;
  PD::MR local_recv_mr;
  // MRHandle local_mr_h = {};
  // MRHandle remote_mr_h = {};

  ConnectionPtr conn = nullptr;
  rdma_conn_param local = {};
  rdma_conn_param remote = {};
  ibv_device_attr_ex device_attr_ex = {};

  uint32_t active_send_buffer_idx = 0;
  uint32_t active_recv_buffer_idx = 0;
};

inline ConnectionPtr Connection::establish(Side side, rdma_cm_id* id, Endpoint& endpoint) {
  EventChannel c(id->channel);
  endpoint.setup_resources(id);
  switch (side) {
    case Side::ClientSide: {
      if (auto ec = rdma_connect(id, &endpoint.local); ec < 0) {
        die("Fail to establish connection, errno: {}", errno);
      }
      auto e = c.wait(RDMA_CM_EVENT_ESTABLISHED);
      endpoint.setup_remote_param(e->param.conn);
      c.ack(e);
    } break;
    case Side::ServerSide: {
      if (auto ec = rdma_accept(id, &endpoint.local); ec < 0) {
        die("Fail to accept connection, errno: {}", errno);
      }
      c.wait_and_ack(RDMA_CM_EVENT_ESTABLISHED);
    } break;
  }
  // INFO("local 0x{:0X} {} {}", endpoint.local_mr_h.address(), endpoint.local_mr_h.length(),
  // endpoint.local_mr_h.rkey()); INFO("remote 0x{:0X} {} {}", endpoint.remote_mr_h.address(),
  // endpoint.remote_mr_h.length(),
  //      endpoint.remote_mr_h.rkey());

  return ConnectionPtr(new Connection(side, id));
}

class ConnectionHandle : Noncopyable, Nonmovable {
 public:
  ConnectionHandle(Side side) : side(side) {}
  ~ConnectionHandle() = default;

  rdma_cm_id* setup_and_bind(std::string_view ip, uint16_t port) {
    rdma_cm_id* id = nullptr;
    if (auto ec = rdma_create_id(c.underlying(), &id, nullptr, RDMA_PS_TCP); ec < 0) {
      die("Fail to create cm id, errno: {}", errno);
    }
    rdma_addrinfo hints = {};
    hints.ai_flags = RAI_NUMERICHOST | RAI_FAMILY | (side == Side::ServerSide ? RAI_PASSIVE : 0);
    hints.ai_family = AF_INET;
    auto port_str = std::to_string(port);
    rdma_addrinfo* addr = nullptr;
    if (auto ec = rdma_getaddrinfo(ip.data(), port_str.data(), &hints, &addr); ec < 0) {
      die("Fail to get addrinfo, errno: {}", errno);
    }
    if (auto ec = rdma_bind_addr(id, (side == Side::ServerSide ? addr->ai_src_addr : addr->ai_dst_addr)); ec < 0) {
      die("Fail to bind {}:{}, errno: {}", ip, port, errno);
    }
    rdma_freeaddrinfo(addr);
    return id;
  }

 protected:
  Side side;
  EventChannel c;
};

class Acceptor : public ConnectionHandle {
 public:
  Acceptor(std::string local_ip, uint16_t local_port)
      : ConnectionHandle(Side::ServerSide), id(setup_and_bind(local_ip, local_port)) {}
  ~Acceptor() {
    if (id != nullptr) {
      if (auto ec = rdma_destroy_id(id); ec < 0) {
        die("Fail to destroy listening id, errno: {}", errno);
      }
    }
  }

 public:
  Acceptor& associate(EndpointRefs&& endpoints_) {
    endpoints = std::move(endpoints_);
    return *this;
  }

  void listen_and_accept() {
    if (auto ec = rdma_listen(id, 10); ec < 0) {
      die("Fail to listen, errno: {}", errno);
    }
    for (auto& endpoint_ref : endpoints) {
      auto& endpoint = endpoint_ref.get();
      auto e = c.wait(RDMA_CM_EVENT_CONNECT_REQUEST);
      endpoint.setup_remote_param(e->param.conn);
      endpoint.conn = Connection::establish(side, e->id, endpoint);
      c.ack(e);
    }
  }

 private:
  EndpointRefs endpoints;
  rdma_cm_id* id = nullptr;
};

class Connector : public ConnectionHandle {
 public:
  Connector(std::string remote_ip, uint16_t remote_port)
      : ConnectionHandle(Side::ClientSide),
        remote_addr_in({
            .sin_family = AF_INET,
            .sin_port = htons(remote_port),
            .sin_addr = {.s_addr = inet_addr(remote_ip.data())},
            .sin_zero = {},
        }) {}

  ~Connector() = default;

  void connect(Endpoint& endpoint, std::string local_ip, uint16_t local_port = 0) {
    auto id = setup_and_bind(local_ip, local_port);
    if (auto ec = rdma_resolve_addr(id, nullptr, reinterpret_cast<sockaddr*>(&remote_addr_in), 10); ec < 0) {
      die("Fail to resolve addr {}, errno: {}", inet_ntoa(remote_addr_in.sin_addr), ntohs(remote_addr_in.sin_port),
          errno);
    }
    c.wait_and_ack(RDMA_CM_EVENT_ADDR_RESOLVED);
    if (auto ec = rdma_resolve_route(id, 10); ec < 0) {
      die("Fail to resolve route, errno: {}", errno);
    }
    c.wait_and_ack(RDMA_CM_EVENT_ROUTE_RESOLVED);
    endpoint.conn = Connection::establish(side, id, endpoint);
  }

 private:
  sockaddr_in remote_addr_in = {};
};
