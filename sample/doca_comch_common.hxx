#pragma once

#include <dbg.h>

#include <memory>

#include "memory/doca_simple_buffer.hxx"
#include "priv/common.hxx"
#include "util/doca_check.hxx"
#include "util/doca_wrapper.hxx"
#include "util/logger.hxx"
#include "util/unreachable.hxx"

using namespace std::chrono_literals;
using namespace doca_wrapper;

class Endpoint;
using EndpointRef = std::reference_wrapper<Endpoint>;
using EndpointRefs = std::vector<EndpointRef>;
using Id2EndpointRef = std::unordered_map<uint32_t, EndpointRef>;

class Connection;
using ConnectionPtr = std::unique_ptr<Connection>;

namespace {

inline void default_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states prev_state,
                                    doca_ctx_states next_state) {
  TRACE("State change: {} -> {}", prev_state, next_state);
  // switch (next_state) {
  //   case DOCA_CTX_STATE_IDLE:
  //   case DOCA_CTX_STATE_STARTING:
  //   case DOCA_CTX_STATE_RUNNING:
  //   case DOCA_CTX_STATE_STOPPING:
  // }
}

}  // namespace

// own device&device representor, and shared between endpoints and connection handles

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;
  friend class DocaComch;

 public:
  Endpoint(DocaComch &comch_, size_t n_qe, size_t max_payload_size)
      : comch(comch_),
        buffers(comch_.dev, n_qe * 2, max_payload_size),
        producer_pe(create_pe()),
        consumer_pe(create_pe()),
        producer(create_comch_producer(comch.connection)),
        consumer(create_comch_consumer(comch.connection, buffers)) {
    INFO("started!!");
  }
  ~Endpoint() { stop(); }

  bool progress() {
    auto p1 = doca_pe_progress(producer_pe.get());
    auto p2 = doca_pe_progress(consumer_pe.get());
    return p1 > 0 || p2 > 0;
  }

 private:
  void start() {
    {
      doca_check(doca_comch_producer_task_send_set_conf(producer.get(), producer_send_task_comp_cb,
                                                        producer_send_task_err_cb, buffers.size() / 2));
      auto ctx = doca_comch_producer_as_ctx(producer.get());
      doca_check(doca_ctx_set_state_changed_cb(ctx, default_state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_pe_connect_ctx(producer_pe.get(), ctx));
      doca_check(doca_ctx_start(ctx));
    }
    {
      doca_check(doca_comch_consumer_task_post_recv_set_conf(consumer.get(), consumer_post_recv_task_comp_cb,
                                                             consumer_post_recv_task_err_cb, buffers.size() / 2));
      auto ctx = doca_comch_consumer_as_ctx(consumer.get());
      doca_check(doca_ctx_set_state_changed_cb(ctx, default_state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_pe_connect_ctx(consumer_pe.get(), ctx));
      doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
    }
  }

  void prepare() { EndpointBase::prepare(); }

  void run() { EndpointBase::run(); }

  void stop() {
    doca_check(doca_ctx_stop(doca_comch_producer_as_ctx(producer.get())));
    doca_check(doca_ctx_stop(doca_comch_consumer_as_ctx(consumer.get())));
    EndpointBase::stop();
  }

  static void producer_send_task_comp_cb(struct doca_comch_producer_task_send *, union doca_data, union doca_data);
  static void producer_send_task_err_cb(struct doca_comch_producer_task_send *, union doca_data, union doca_data);
  static void consumer_post_recv_task_comp_cb(struct doca_comch_consumer_task_post_recv *, union doca_data,
                                              union doca_data);
  static void consumer_post_recv_task_err_cb(struct doca_comch_consumer_task_post_recv *, union doca_data,
                                             union doca_data);
  DocaComch &comch;
  MmapBuffers buffers;
  Pe producer_pe;
  Pe consumer_pe;
  DocaComchProducer producer;
  DocaComchConsumer consumer;
  ConnectionPtr conn;
};

class Connection : public ConnectionBase {
  friend class Acceptor;
  friend class Connector;
  friend class Endpoint;

 public:
  ~Connection() = default;

  template <Side side>
  static void establish(uint32_t remote_id, Endpoint &e) {
    e.conn = ConnectionPtr(new Connection(side, e.comch.name, get_comch_consumer_id(e.consumer), remote_id));
    TRACE("Connection {} <-> {} established.", e.conn->local_addr, e.conn->remote_addr);
    e.prepare();
  }

 private:
  Connection(Side side_, std::string_view name, uint32_t local_id, uint32_t remote_id)
      : ConnectionBase(side_), id(remote_id) {
    local_addr = std::format("{}:{}", name, local_id);
    remote_addr = std::format("{}:{}", name, remote_id);
  }

  uint32_t id = -1;
};

class Acceptor : ConnectionHandleBase {
  friend class DocaComch;

 public:
  Acceptor(DocaComch &comch_) : ConnectionHandleBase(Side::ServerSide), comch(comch_) {}
  ~Acceptor() = default;

  Acceptor &associate(EndpointRefs &&endpoints) {
    comch.pending_endpoints.insert(comch.pending_endpoints.end(), std::make_move_iterator(endpoints.begin()),
                                   std::make_move_iterator(endpoints.end()));
    return *this;
  }

  void listen_and_accept() {
    comch.progress_until([this]() { return comch.pending_endpoints.empty(); });
  }

 private:
  DocaComch &comch;
};

class Connector : ConnectionHandleBase {
  friend class DocaComch;

 public:
  Connector(DocaComch &comch_) : ConnectionHandleBase(Side::ClientSide), comch(comch_) {}
  ~Connector() = default;

  void connect(Endpoint &e) {
    assert(e.idle());
    e.start();
    comch.pending_endpoints.emplace_back(e);
    comch.progress_until([&e]() { return !e.idle(); });
  }

 private:
  DocaComch &comch;
};


void Endpoint::producer_send_task_comp_cb(struct doca_comch_producer_task_send *task, union doca_data task_user_data,
                                          union doca_data ctx_user_data) {
  auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  auto buf = doca_comch_producer_task_send_get_buf(task);
  INFO("Producer send task done!");
  doca_task_free(doca_comch_producer_task_send_as_task(task));
}

void Endpoint::producer_send_task_err_cb(struct doca_comch_producer_task_send *task, union doca_data task_user_data,
                                         union doca_data ctx_user_data) {
  auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  auto buf = doca_comch_producer_task_send_get_buf(task);
  ERROR("Producer send task failed!");
  doca_task_free(doca_comch_producer_task_send_as_task(task));
}

void Endpoint::consumer_post_recv_task_comp_cb(struct doca_comch_consumer_task_post_recv *task,
                                               union doca_data task_user_data, union doca_data ctx_user_data) {
  auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  auto buf = doca_comch_consumer_task_post_recv_get_buf(task);
  INFO("Consumer post recv task done!");
  doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
}

void Endpoint::consumer_post_recv_task_err_cb(struct doca_comch_consumer_task_post_recv *task,
                                              union doca_data task_user_data, union doca_data ctx_user_data) {
  auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  auto buf = doca_comch_consumer_task_post_recv_get_buf(task);
  ERROR("Consumer post recv task failed!");
  doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
}
