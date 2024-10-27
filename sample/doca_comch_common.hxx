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
class DocaComch {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;
  friend class Endpoint;

 public:
  DocaComch(std::string name_, std::string dev_pci_addr, std::string dev_rep_pci_addr)
      : side(Side::ServerSide),
        name(name_),
        dev(open_doca_dev(dev_pci_addr)),
        dev_rep(open_doca_dev_rep(dev, dev_rep_pci_addr, DOCA_DEVINFO_REP_FILTER_NET)),
        pe(create_pe()),
        server(create_comch_server(dev, dev_rep, name)),
        max_msg_size(comch_server_max_msg_size(dev)),
        recv_queue_size(comch_server_recv_queue_size(server)) {
    dbg(max_msg_size, recv_queue_size);
    start<Side::ServerSide>();
  }
  DocaComch(std::string name_, std::string dev_pci_addr)
      : side(Side::ClientSide),
        name(name_),
        dev(open_doca_dev(dev_pci_addr)),
        pe(create_pe()),
        client(create_comch_client(dev, name)),
        max_msg_size(comch_client_max_msg_size(client)),
        recv_queue_size(comch_client_recv_queue_size(client)) {
    dbg(max_msg_size, recv_queue_size);
    start<Side::ClientSide>();
  }

  ~DocaComch() { stop(); }

 private:
  void progress_until(std::function<bool()> predictor) {
    while (!predictor()) {
      if (!progress()) {
        std::this_thread::sleep_for(1us);
      }
    }
  }
  bool progress() { return doca_pe_progress(pe.get()); }

  // bool idle() { return state() == DOCA_CTX_STATE_IDLE; }
  // bool starting() { return state() == DOCA_CTX_STATE_STARTING; }
  // bool running() { return state() == DOCA_CTX_STATE_RUNNING; }
  // bool stopping() { return state() == DOCA_CTX_STATE_STOPPING; }

  // doca_ctx_states state() {
  //   doca_ctx_states s;
  //   doca_check(doca_ctx_get_state(doca_comch_server_as_ctx(server.get()), &s));
  //   return s;
  // }

  template <Side side>
  void start();

  void stop();

  template <Side side>
  static void state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);

  static void connection_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t);

  static void disconnection_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t);

  template <Side side>
  static void new_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);

  template <Side side>
  static void expired_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);

  template <Side side>
  static void send_task_comp_cb(doca_comch_task_send *, doca_data, doca_data);

  template <Side side>
  static void send_task_err_cb(doca_comch_task_send *, doca_data, doca_data);

  template <Side side>
  static void msg_recv_cb(struct doca_comch_event_msg_recv *, uint8_t *, uint32_t, struct doca_comch_connection *);

  Side side;
  std::string name;
  DocaDev dev;
  DocaDevRep dev_rep;
  DocaPe pe;  // used for server/client
  // ctrl path
  union {
    DocaComchServer server;
    DocaComchClient client;
  };
  uint32_t max_msg_size = 0;
  uint32_t recv_queue_size = 0;
  // data path
  DocaComchConnection connection = nullptr;  // only support one connection now
  // used in callbacks
  EndpointRefs pending_endpoints;        // wait for connection
  Id2EndpointRef established_endpoints;  // established
};

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
        consumer_pe(create_pe()) {}
  ~Endpoint() {}

 private:
  void prepare() {
    {
      doca_check(doca_comch_producer_task_send_set_conf(producer.get(), producer_send_task_comp_cb,
                                                        producer_send_task_err_cb, buffers.size() / 2));
      auto ctx = doca_comch_producer_as_ctx(producer.get());
      doca_check(doca_ctx_set_state_changed_cb(ctx, default_state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data{.ptr = this}));
      doca_check(doca_pe_connect_ctx(producer_pe.get(), ctx));
      doca_check(doca_ctx_start(ctx));
    }
    {
      doca_check(doca_comch_consumer_task_post_recv_set_conf(consumer.get(), consumer_post_recv_task_comp_cb,
                                                             consumer_post_recv_task_err_cb, buffers.size() / 2));
      auto ctx = doca_comch_consumer_as_ctx(consumer.get());
      doca_check(doca_ctx_set_state_changed_cb(ctx, default_state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data{.ptr = this}));
      doca_check(doca_pe_connect_ctx(consumer_pe.get(), ctx));
      doca_check(doca_ctx_start(ctx));
    }
    EndpointBase::prepare();
  }

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
  DocaPe producer_pe;
  DocaPe consumer_pe;
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
    comch.pending_endpoints.emplace_back(e);
    e.producer = create_comch_producer(comch.connection);
    e.consumer = create_comch_consumer(comch.connection, e.buffers);
    comch.progress_until([&e]() { return !e.idle(); });
  }

 private:
  DocaComch &comch;
};

template <Side side>
void DocaComch::start() {
  assert(side == this->side);

  doca_ctx *ctx = nullptr;
  if constexpr (side == Side::ServerSide) {
    ctx = doca_comch_server_as_ctx(server.get());
  } else if constexpr (side == Side::ClientSide) {
    ctx = doca_comch_client_as_ctx(client.get());
  } else {
    static_unreachable;
  }

  doca_check(doca_pe_connect_ctx(pe.get(), ctx));

  if constexpr (side == Side::ServerSide) {
    doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb<side>));
    doca_check(doca_comch_server_task_send_set_conf(server.get(), send_task_comp_cb<side>, send_task_err_cb<side>, 64));
    doca_check(doca_comch_server_event_msg_recv_register(server.get(), msg_recv_cb<side>));
    doca_check(doca_comch_server_event_connection_status_changed_register(server.get(), connection_event_cb,
                                                                          disconnection_event_cb));
    doca_check(
        doca_comch_server_event_consumer_register(server.get(), new_consumer_cb<side>, expired_consumer_cb<side>));
    doca_check(doca_comch_server_set_max_msg_size(server.get(), max_msg_size));
    doca_check(doca_comch_server_set_recv_queue_size(server.get(), recv_queue_size));
  } else if constexpr (side == Side::ClientSide) {
    doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb<side>));
    doca_check(doca_comch_client_task_send_set_conf(client.get(), send_task_comp_cb<side>, send_task_err_cb<side>, 64));
    doca_check(doca_comch_client_event_msg_recv_register(client.get(), msg_recv_cb<side>));
    doca_check(
        doca_comch_client_event_consumer_register(client.get(), new_consumer_cb<side>, expired_consumer_cb<side>));
    doca_check(doca_comch_client_set_max_msg_size(client.get(), max_msg_size));
    doca_check(doca_comch_client_set_recv_queue_size(client.get(), recv_queue_size));
  } else {
    static_unreachable;
  }

  doca_check(doca_ctx_set_user_data(ctx, doca_data{.ptr = this}));

  if constexpr (side == Side::ServerSide) {
    doca_check(doca_ctx_start(ctx));
  } else if constexpr (side == Side::ClientSide) {
    doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
  } else {
    static_unreachable;
  }

  progress_until([this]() { return connection != nullptr; });
}

inline void DocaComch::stop() {
  if (side == Side::ServerSide) {
    doca_check(doca_ctx_stop(doca_comch_server_as_ctx(server.get())));
  } else if (side == Side::ClientSide) {
    doca_check(doca_ctx_stop(doca_comch_client_as_ctx(client.get())));
  }
}

template <Side side>
void DocaComch::state_change_cb(const doca_data, doca_ctx *ctx, doca_ctx_states prev_state,
                                doca_ctx_states next_state) {
  auto comch = reinterpret_cast<DocaComch *>(ctx);
  TRACE("State change: {} -> {}", prev_state, next_state);
  if constexpr (side == Side::ServerSide) {
    switch (next_state) {
      case DOCA_CTX_STATE_IDLE: {
      } break;
      case DOCA_CTX_STATE_STARTING: {
      } break;
      case DOCA_CTX_STATE_RUNNING: {
      } break;
      case DOCA_CTX_STATE_STOPPING: {
      } break;
    }
  } else if constexpr (side == Side::ClientSide) {
    switch (next_state) {
      case DOCA_CTX_STATE_IDLE: {
      } break;
      case DOCA_CTX_STATE_STARTING: {
      } break;
      case DOCA_CTX_STATE_RUNNING: {
        doca_check(doca_comch_client_get_connection(comch->client.get(), &comch->connection));
      } break;
      case DOCA_CTX_STATE_STOPPING: {
      } break;
    }
  } else {
    static_unreachable;
  }
}

inline void DocaComch::connection_event_cb(doca_comch_event_connection_status_changed *,
                                           doca_comch_connection *connection, uint8_t success) {
  if (!success) {
    ERROR("Connection failure");
    return;
  }
  auto comch = reinterpret_cast<DocaComch *>(get_user_data_from_connection<Side::ServerSide>(connection));
  comch->connection = connection;
  TRACE("Establish connection");
}

inline void DocaComch::disconnection_event_cb(doca_comch_event_connection_status_changed *,
                                              doca_comch_connection *connection, uint8_t success) {
  [[maybe_unused]] auto comch = get_user_data_from_connection<Side::ServerSide>(connection);
  if (!success) {
    ERROR("Disconnection failure");
  }
  TRACE("Disconnection ok");
}

template <Side side>
void DocaComch::new_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *connection, uint32_t id) {
  auto comch = reinterpret_cast<DocaComch *>(get_user_data_from_connection<side>(connection));
  assert(connection == comch->connection);
  assert(!comch->pending_endpoints.empty());
  auto &e = comch->pending_endpoints.back().get();
  if constexpr (side == Side::ServerSide) {
    e.producer = create_comch_producer(connection);
    e.consumer = create_comch_consumer(connection, e.buffers);
  }
  Connection::establish<side>(id, e);
  comch->pending_endpoints.pop_back();
  comch->established_endpoints.emplace(id, e);
}

template <Side side>
void DocaComch::expired_consumer_cb(doca_comch_event_consumer *, doca_comch_connection *connection, uint32_t id) {
  auto comch = reinterpret_cast<DocaComch *>(get_user_data_from_connection<side>(connection));
  INFO("Consumer {} expired", id);
  auto iter = comch->established_endpoints.find(id);
  assert(iter != comch->established_endpoints.end());
  iter->second.get().stop();
  comch->established_endpoints.erase(iter);
}

template <Side side>
void DocaComch::send_task_comp_cb(doca_comch_task_send *task, doca_data task_user_data, doca_data ctx_user_data) {
  auto comch = reinterpret_cast<DocaComch *>(ctx_user_data.ptr);
  INFO("Task send done!");
  doca_task_free(doca_comch_task_send_as_task(task));
}

template <Side side>
void DocaComch::send_task_err_cb(doca_comch_task_send *task, doca_data task_user_data, doca_data ctx_user_data) {
  auto comch = reinterpret_cast<DocaComch *>(ctx_user_data.ptr);
  ERROR("Task send failed!");
  doca_task_free(doca_comch_task_send_as_task(task));
}

template <Side side>
void DocaComch::msg_recv_cb(struct doca_comch_event_msg_recv *, uint8_t *recv_buffer, uint32_t msg_len,
                            struct doca_comch_connection *connection) {
  INFO("Message received!");
}

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

template <>
struct std::formatter<doca_ctx_states> : std::formatter<const char *> {
  template <typename Context>
  Context::iterator format(doca_ctx_states s, Context out) const {
    switch (s) {
      case DOCA_CTX_STATE_IDLE:
        return std::formatter<const char *>::format("Idle", out);
      case DOCA_CTX_STATE_STARTING:
        return std::formatter<const char *>::format("Starting", out);
      case DOCA_CTX_STATE_RUNNING:
        return std::formatter<const char *>::format("Running", out);
      case DOCA_CTX_STATE_STOPPING:
        return std::formatter<const char *>::format("Stopping", out);
    }
  }
};
