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

  void progress_until(std::function<bool()> &&predictor) {
    while (!predictor()) {
      if (!progress()) {
        std::this_thread::sleep_for(1us);
      }
    }
  }

  bool progress() {
    auto p1 = doca_pe_progress(producer_pe.get());
    auto p2 = doca_pe_progress(consumer_pe.get());
    return p1 > 0 || p2 > 0;
  }

 private:
  doca_ctx *producer_as_doca_ctx() { return doca_comch_producer_as_ctx(producer.get()); }
  doca_ctx *consumer_as_doca_ctx() { return doca_comch_consumer_as_ctx(consumer.get()); }

  void prepare() {
    {
      
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

  Acceptor &associate(EndpointRefs<Side::ServerSide> &&es) {
    for (auto &e : es) {
      comch.add_data_path_endpoint(e.get());
      endpoints.emplace_back(std::move(e));
    }
    return *this;
  }

  void listen_and_accept() {
    while (true) {
      comch.progress();
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
    while (!e.running()) {
      comch.progress();
      e.progress();
    }
  }

 private:
  ctrl_path::Endpoint<Side::ClientSide> &comch;
};

}  // namespace data_path
