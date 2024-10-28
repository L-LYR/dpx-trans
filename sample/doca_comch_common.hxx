#pragma once

#include <dbg.h>

#include <memory>

#include "doca_comch_ctrl_common.hxx"
#include "memory/doca_simple_buffer.hxx"
#include "priv/common.hxx"
#include "util/doca_check.hxx"
#include "util/doca_wrapper.hxx"
#include "util/logger.hxx"

using namespace std::chrono_literals;
using namespace doca_wrapper;

namespace data_path {

template <Side side>
class Endpoint;
template <Side side>
using EndpointRef = std::reference_wrapper<Endpoint<side>>;
template <Side side>
using EndpointRefs = std::vector<EndpointRef<side>>;
template <Side side>
using Id2EndpointRef = std::unordered_map<uint32_t, EndpointRef<side>>;

class Connection;
using ConnectionPtr = std::unique_ptr<Connection>;

template <Side side>
class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;

 public:
  Endpoint(ctrl_path::Endpoint<side> &comch_, MmapBuffers &&buffers_)
      : comch(comch_),
        buffers(std::move(buffers_)),
        producer_pe(create_pe()),
        consumer_pe(create_pe()),
        producer(create_comch_producer(comch.connection)),
        consumer(create_comch_consumer(comch.connection, buffers)) {}
  ~Endpoint() { close(); }

  bool progress() {
    auto p0 = comch->progress();
    auto p1 = doca_pe_progress(producer_pe.get());
    auto p2 = doca_pe_progress(consumer_pe.get());
    return p0 || p1 > 0 || p2 > 0;
  }

 private:
  void prepare() {
    {
      doca_check(doca_comch_producer_task_send_set_conf(producer.get(), producer_send_task_comp_cb,
                                                        producer_send_task_err_cb, buffers.size() / 2));
      auto ctx = doca_comch_producer_as_ctx(producer.get());
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb<"producer">));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_pe_connect_ctx(producer_pe.get(), ctx));
      doca_check(doca_ctx_start(ctx));
    }
    {
      doca_check(doca_comch_consumer_task_post_recv_set_conf(consumer.get(), consumer_post_recv_task_comp_cb,
                                                             consumer_post_recv_task_err_cb, buffers.size() / 2));
      auto ctx = doca_comch_consumer_as_ctx(consumer.get());
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb<"consumer">));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_pe_connect_ctx(consumer_pe.get(), ctx));
      doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
    }
    EndpointBase::prepare();
  }

  void run() { EndpointBase::run(); }

  void close() {
    doca_check(doca_ctx_stop(doca_comch_producer_as_ctx(producer.get())));
    doca_check(doca_ctx_stop(doca_comch_consumer_as_ctx(consumer.get())));
  }

  template <const char *what>
  static void state_change_cb(const doca_data ctx_user_data, doca_ctx *, doca_ctx_states prev_state,
                              doca_ctx_states next_state) {
    auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    TRACE("DOCA Comch {} {} {} state change: {} -> {}", e->comch.name, side, what, prev_state, next_state);
  }

  static void producer_send_task_comp_cb(struct doca_comch_producer_task_send *task, union doca_data task_user_data,
                                         union doca_data ctx_user_data) {
    auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    auto buf = doca_comch_producer_task_send_get_buf(task);
    INFO("Producer send task done!");
    doca_task_free(doca_comch_producer_task_send_as_task(task));
  }
  static void producer_send_task_err_cb(struct doca_comch_producer_task_send *task, union doca_data task_user_data,
                                        union doca_data ctx_user_data) {
    auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    auto buf = doca_comch_producer_task_send_get_buf(task);
    ERROR("Producer send task failed!");
    doca_task_free(doca_comch_producer_task_send_as_task(task));
  }
  static void consumer_post_recv_task_comp_cb(struct doca_comch_consumer_task_post_recv *task,
                                              union doca_data task_user_data, union doca_data ctx_user_data) {
    auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    auto buf = doca_comch_consumer_task_post_recv_get_buf(task);
    INFO("Consumer post recv task done!");
    doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
  }
  static void consumer_post_recv_task_err_cb(struct doca_comch_consumer_task_post_recv *task,
                                             union doca_data task_user_data, union doca_data ctx_user_data) {
    auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    auto buf = doca_comch_consumer_task_post_recv_get_buf(task);
    ERROR("Consumer post recv task failed!");
    doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
  }

  ctrl_path::Endpoint<side> &comch;
  MmapBuffers buffers;
  Pe producer_pe;
  Pe consumer_pe;
  DocaComchProducer producer;
  DocaComchConsumer consumer;
};

class Acceptor : ConnectionHandleBase<Side::ServerSide> {
  friend class DocaComch;

 public:
  Acceptor() = default;
  ~Acceptor() = default;

  Acceptor &associate(EndpointRefs<Side::ServerSide> &&es) {}

  void listen_and_accept() {}
};

class Connector : ConnectionHandleBase<Side::ClientSide> {
  friend class DocaComch;

 public:
  Connector() = default;
  ~Connector() = default;

  void connect(Endpoint<Side::ClientSide> &e) {}
};

}  // namespace data_path
