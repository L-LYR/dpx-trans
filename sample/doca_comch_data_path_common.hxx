#pragma once

#include <dbg.h>

#include "doca_comch_ctrl_path_common.hxx"
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

template <Side side>
class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;
  friend class ctrl_path::Endpoint<side>;

 public:
  Endpoint(ctrl_path::Endpoint<side> &comch_, MmapBuffers &&buffers_)
      : comch(comch_),
        buffers(std::move(buffers_)),
        producer_pe(create_pe()),
        consumer_pe(create_pe()),
        producer(create_comch_producer(comch.conn)),
        consumer(create_comch_consumer(comch.conn, buffers)) {}
  ~Endpoint() { close(); }

 private:
  void progress_until(std::function<bool()> &&predictor) {
    while (!predictor()) {
      if (!progress()) {
        std::this_thread::sleep_for(1us);
      }
    }
  }

  bool progress() {
    auto p0 = comch.progress();
    auto p1 = doca_pe_progress(producer_pe.get());
    auto p2 = doca_pe_progress(consumer_pe.get());
    return p0 || p1 > 0 || p2 > 0;
  }

  doca_ctx *producer_as_doca_ctx() { return doca_comch_producer_as_ctx(producer.get()); }
  doca_ctx *consumer_as_doca_ctx() { return doca_comch_consumer_as_ctx(consumer.get()); }

  void prepare() {
    {
      doca_check(doca_comch_producer_task_send_set_conf(producer.get(), producer_send_task_comp_cb,
                                                        producer_send_task_err_cb, buffers.size() / 2));
      auto ctx = producer_as_doca_ctx();
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_pe_connect_ctx(producer_pe.get(), ctx));
      doca_check(doca_ctx_start(ctx));
    }
    {
      doca_check(doca_comch_consumer_task_post_recv_set_conf(consumer.get(), consumer_post_recv_task_comp_cb,
                                                             consumer_post_recv_task_err_cb, buffers.size() / 2));
      auto ctx = consumer_as_doca_ctx();
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_pe_connect_ctx(consumer_pe.get(), ctx));
      doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
    }
    EndpointBase::prepare();
  }

  void close() {
    if (stopped()) {
      return;
    }
    doca_check(doca_ctx_stop(doca_comch_producer_as_ctx(producer.get())));
    doca_check(doca_ctx_stop(doca_comch_consumer_as_ctx(consumer.get())));
    progress_until([this]() { return stopped(); });
  }

  static void state_change_cb(const doca_data ctx_user_data, doca_ctx *ctx, doca_ctx_states prev_state,
                              doca_ctx_states next_state) {
    auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    TRACE("DOCA Comch {} {} {} state change: {} -> {}", e->comch.name, side,
          (ctx == e->producer_as_doca_ctx() ? "producer" : "consumer"), prev_state, next_state);
  }

  static void producer_send_task_comp_cb(struct doca_comch_producer_task_send *task, union doca_data,
                                         union doca_data ctx_user_data) {
    [[maybe_unused]] auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    [[maybe_unused]] auto buf = doca_comch_producer_task_send_get_buf(task);
    INFO("Producer send task done!");
    doca_task_free(doca_comch_producer_task_send_as_task(task));
  }
  static void producer_send_task_err_cb(struct doca_comch_producer_task_send *task, union doca_data,
                                        union doca_data ctx_user_data) {
    [[maybe_unused]] auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    [[maybe_unused]] auto buf = doca_comch_producer_task_send_get_buf(task);
    ERROR("Producer send task failed!");
    doca_task_free(doca_comch_producer_task_send_as_task(task));
  }
  static void consumer_post_recv_task_comp_cb(struct doca_comch_consumer_task_post_recv *task, union doca_data,
                                              union doca_data ctx_user_data) {
    [[maybe_unused]] auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    [[maybe_unused]] auto buf = doca_comch_consumer_task_post_recv_get_buf(task);
    INFO("Consumer post recv task done!");
    doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
  }
  static void consumer_post_recv_task_err_cb(struct doca_comch_consumer_task_post_recv *task, union doca_data,
                                             union doca_data ctx_user_data) {
    [[maybe_unused]] auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    [[maybe_unused]] auto buf = doca_comch_consumer_task_post_recv_get_buf(task);
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
  Acceptor(ctrl_path::Endpoint<Side::ServerSide> &comch_) : comch(comch_) {}
  ~Acceptor() = default;

  Acceptor &associate(EndpointRefs<Side::ServerSide> &&endpoints) {
    for (auto &endpoint : endpoints) {
      auto &e = endpoint.get();
      assert(&e.comch == &comch);
      e.prepare();
      comch.add_data_path_endpoint(e);
      endpoints.push_back(std::move(endpoint));
    }
    return *this;
  }

  void listen_and_accept() {
    while (true) {
      bool all_running = true;
      for (auto &endpoint : endpoints) {
        auto &e = endpoint.get();
        if (!e.running()) {
          e.progress();
          all_running = false;
        }
      }
      if (all_running) {
        break;
      }
      std::this_thread::sleep_for(1us);
    }
    endpoints.clear();
  }

 private:
  ctrl_path::Endpoint<Side::ServerSide> &comch;
  EndpointRefs<Side::ServerSide> endpoints;
};

class Connector : ConnectionHandleBase<Side::ClientSide> {
  friend class DocaComch;

 public:
  Connector(ctrl_path::Endpoint<Side::ClientSide> &comch_) : comch(comch_) {}
  ~Connector() = default;

  void connect(Endpoint<Side::ClientSide> &e) {
    assert(&e.comch == &comch);
    e.prepare();
    comch.add_data_path_endpoint(e);
    e.progress_until([&e]() { return e.running(); });
  }

 private:
  ctrl_path::Endpoint<Side::ClientSide> &comch;
};

}  // namespace data_path
