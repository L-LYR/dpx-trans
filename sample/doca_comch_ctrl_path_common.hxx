#pragma once

#include <dbg.h>

#include <functional>
#include <thread>

#include "priv/common.hxx"
#include "util/doca_wrapper.hxx"
#include "util/doca_wrapper_def.hxx"
#include "util/unreachable.hxx"

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

}  // namespace data_path

namespace ctrl_path {

template <Side side>
class Endpoint;
template <Side side>
using EndpointRef = std::reference_wrapper<Endpoint<side>>;
template <Side side>
using EndpointRefs = std::vector<EndpointRef<side>>;

}  // namespace ctrl_path

class Device {
  template <Side side>
  friend class ctrl_path::Endpoint;

 public:
  explicit Device(std::string_view dev_pci_addr) : dev(open_dev(dev_pci_addr)) {}

  Device(std::string_view dev_pci_addr, std::string_view dev_rep_pci_addr, doca_devinfo_rep_filter filter)
      : dev(open_dev(dev_pci_addr)), rep(open_dev_rep(dev, dev_rep_pci_addr, filter)) {}

  ~Device() {}
  // TODO make private
  DocaDev dev;
  DocaDevRep rep;
};

namespace ctrl_path {

template <Side side>
class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;
  friend class data_path::Endpoint<side>;

 public:
  Endpoint(std::string name_, Device &dev_)
    requires(side == Side::ServerSide)
      : name(name_),
        dev(dev_),
        pe(create_pe()),
        comch(create_comch_server(dev.dev, dev.rep, name)),
        max_msg_size(device_comch_max_msg_size(dev.dev)),
        recv_queue_size(32) {}
  Endpoint(std::string name_, Device &dev_)
    requires(side == Side::ClientSide)
      : name(name_),
        dev(dev_),
        pe(create_pe()),
        comch(create_comch_client(dev.dev, name)),
        max_msg_size(device_comch_max_msg_size(dev.dev)),
        recv_queue_size(32) {}
  ~Endpoint() { close(); }

  void add_data_path_endpoint(data_path::EndpointRef<side> &&e) {
    assert(running());
    pending_endpoints.emplace_back(e);
  }

 private:
  void progress_until(std::function<bool()> &&predictor) {
    while (!predictor()) {
      if (!progress()) {
        std::this_thread::sleep_for(1us);
      }
    }
  }

  bool progress() { return doca_pe_progress(pe.get()); }

  doca_ctx *as_doca_context() {
    if constexpr (side == Side::ServerSide) {
      return doca_comch_server_as_ctx(comch.get());
    } else if constexpr (side == Side::ClientSide) {
      return doca_comch_client_as_ctx(comch.get());
    } else {
      static_unreachable;
    }
  }

  // endpoint from Idle to Ready, endpoint is ready to establish connection
  void prepare();

  void close();

  static void state_change_cb(const doca_data ctx_user_data, doca_ctx *, doca_ctx_states prev_state,
                              doca_ctx_states next_state);

  static void connection_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t)
    requires(side == Side::ServerSide);

  static void disconnection_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t)
    requires(side == Side::ServerSide);

  static void new_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);

  static void expired_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);

  static void send_task_comp_cb(doca_comch_task_send *, doca_data, doca_data);

  static void send_task_err_cb(doca_comch_task_send *, doca_data, doca_data);

  static void msg_recv_cb(struct doca_comch_event_msg_recv *, uint8_t *, uint32_t, struct doca_comch_connection *);

  using ComchType = std::conditional_t<side == Side::ServerSide, ComchServer,
                                       std::conditional_t<side == Side::ClientSide, ComchClient, void>>;

  std::string name;
  Device &dev;
  Pe pe;
  ComchType comch;
  uint32_t max_msg_size = 0;
  uint32_t recv_queue_size = 0;
  ComchConnection conn = nullptr;

  // for data path usage
  data_path::EndpointRefs<side> pending_endpoints;
  data_path::Id2EndpointRef<side> established_endpoints;
};

class Acceptor : ConnectionHandleBase<Side::ServerSide> {
 public:
  Acceptor() = default;
  ~Acceptor() = default;
  Acceptor &associate(EndpointRefs<Side::ServerSide> &&es) {
    // drive endpoints_ from idle to running, server side do not has starting
    for (auto &e : es) {
      e.get().prepare();
      endpoints.emplace_back(e);
    }
    return *this;
  }
  void listen_and_accept() {
    while (true) {
      bool all_established = true;
      for (auto &endpoint : endpoints) {
        auto &e = endpoint.get();
        if (!e.running()) {
          all_established = false;
          e.progress();
        }
      }
      if (all_established) {
        break;
      }
      std::this_thread::sleep_for(10us);
    }
    endpoints.clear();
  }

 private:
  EndpointRefs<Side::ServerSide> endpoints;
};

class Connector : ConnectionHandleBase<Side::ClientSide> {
 public:
  Connector() = default;
  ~Connector() = default;
  void connect(Endpoint<Side::ClientSide> &e) {
    e.prepare();
    e.progress_until([&e] { return e.running(); });
  }
};

}  // namespace ctrl_path

#include "doca_comch_data_path_common.hxx"

namespace ctrl_path {

template <Side side>
void Endpoint<side>::close() {
  if (stopped()) {
    return;
  }
  if constexpr (side == Side::ServerSide) {
    progress_until([this]() { return conn == nullptr; });  // wait for remote to disconnection
    doca_check_ext(doca_ctx_stop(as_doca_context()), DOCA_ERROR_IN_PROGRESS);
  } else if constexpr (side == Side::ClientSide) {
    doca_check_ext(doca_ctx_stop(as_doca_context()), DOCA_ERROR_IN_PROGRESS);
  } else {
    static_unreachable;
  }
  progress_until([this]() { return stopped(); });
}

template <Side side>
void Endpoint<side>::prepare() {
  doca_ctx *ctx = as_doca_context();
  if constexpr (side == Side::ServerSide) {
    doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
    doca_check(doca_comch_server_task_send_set_conf(comch.get(), send_task_comp_cb, send_task_err_cb, 64));
    doca_check(doca_comch_server_event_msg_recv_register(comch.get(), msg_recv_cb));
    doca_check(doca_comch_server_event_connection_status_changed_register(comch.get(), connection_event_cb,
                                                                          disconnection_event_cb));
    doca_check(doca_comch_server_event_consumer_register(comch.get(), new_consumer_cb, expired_consumer_cb));
    doca_check(doca_comch_server_set_max_msg_size(comch.get(), max_msg_size));
    doca_check(doca_comch_server_set_recv_queue_size(comch.get(), recv_queue_size));
  } else if constexpr (side == Side::ClientSide) {
    doca_check(doca_comch_client_task_send_set_conf(comch.get(), send_task_comp_cb, send_task_err_cb, 64));
    doca_check(doca_comch_client_event_msg_recv_register(comch.get(), msg_recv_cb));
    doca_check(doca_comch_client_event_consumer_register(comch.get(), new_consumer_cb, expired_consumer_cb));
    doca_check(doca_comch_client_set_max_msg_size(comch.get(), max_msg_size));
    doca_check(doca_comch_client_set_recv_queue_size(comch.get(), recv_queue_size));
  } else {
    static_unreachable;
  }
  doca_check(doca_pe_connect_ctx(pe.get(), ctx));
  doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
  doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
  if constexpr (side == Side::ServerSide) {
    doca_check(doca_ctx_start(ctx));
  } else if constexpr (side == Side::ClientSide) {
    doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
  } else {
    static_unreachable;
  }
  EndpointBase::prepare();
}

template <Side side>
void Endpoint<side>::state_change_cb(const doca_data ctx_user_data, doca_ctx *, doca_ctx_states prev_state,
                                     doca_ctx_states next_state) {
  auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  TRACE("DOCA Comch {} {} state change: {} -> {}", e->name, side, prev_state, next_state);
  switch (next_state) {
    case DOCA_CTX_STATE_IDLE: {
      if constexpr (side == Side::ClientSide) {
        e->conn = nullptr;
      }
      e->stop();
    } break;
    case DOCA_CTX_STATE_STARTING: {
    } break;
    case DOCA_CTX_STATE_RUNNING: {
      if constexpr (side == Side::ClientSide) {
        doca_check(doca_comch_client_get_connection(e->comch.get(), &e->conn));
        e->run();
      }
    } break;
    case DOCA_CTX_STATE_STOPPING: {
    } break;
  }
}

template <Side side>
void Endpoint<side>::connection_event_cb(doca_comch_event_connection_status_changed *,
                                         doca_comch_connection *connection, uint8_t success)
  requires(side == Side::ServerSide)
{
  if (!success) {
    ERROR("Connection failure");
    return;
  }
  auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection<Side::ServerSide>(connection));
  if (e->conn == nullptr) {
    e->conn = connection;
    e->run();
    TRACE("Establish connection");
  } else {
    WARN("Another connection, ignore");
  }
}

template <Side side>
void Endpoint<side>::disconnection_event_cb(doca_comch_event_connection_status_changed *,
                                            doca_comch_connection *connection, uint8_t success)
  requires(side == Side::ServerSide)
{
  if (!success) {
    ERROR("Disconnection failure");
    return;
  }
  auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection<Side::ServerSide>(connection));
  if (e->conn == connection) {
    e->conn = nullptr;
    TRACE("Disconnection ok");
  } else {
    WARN("Ignored connection, skip");
  }
}

template <Side side>
void Endpoint<side>::new_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *connection, uint32_t id) {
  auto endpoint = reinterpret_cast<Endpoint *>(get_user_data_from_connection<side>(connection));
  assert(connection == endpoint->conn);
  assert(!endpoint->pending_endpoints.empty());
  INFO("Consumer {} get", id);
  auto &data_path_endpoint = endpoint->pending_endpoints.back().get();
  data_path_endpoint.run();
  endpoint->pending_endpoints.pop_back();
  endpoint->established_endpoints.emplace(id, data_path_endpoint);
}

template <Side side>
void Endpoint<side>::expired_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *connection, uint32_t id) {
  auto endpoint = reinterpret_cast<Endpoint *>(get_user_data_from_connection<side>(connection));
  INFO("Consumer {} expired", id);
  auto iter = endpoint->established_endpoints.find(id);
  assert(iter != endpoint->established_endpoints.end());
  iter->second.get().stop();
  endpoint->established_endpoints.erase(iter);
}

template <Side side>
void Endpoint<side>::send_task_comp_cb(doca_comch_task_send *task, doca_data, doca_data ctx_user_data) {
  [[maybe_unused]] auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  INFO("Task send done!");
  doca_task_free(doca_comch_task_send_as_task(task));
}

template <Side side>
void Endpoint<side>::send_task_err_cb(doca_comch_task_send *task, doca_data, doca_data ctx_user_data) {
  [[maybe_unused]] auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  ERROR("Task send failed!");
  doca_task_free(doca_comch_task_send_as_task(task));
}

template <Side side>
void Endpoint<side>::msg_recv_cb(struct doca_comch_event_msg_recv *, uint8_t *, uint32_t,
                                 struct doca_comch_connection *) {
  INFO("Message received!");
}

}  // namespace ctrl_path
