#include "priv/doca_comch/endpoint.hxx"

#include "doca/check.hxx"
#include "util/unreachable.hxx"

namespace {

template <Side side>
inline void *get_user_data_from_connection(doca_comch_connection *c) {
  doca_data user_data(nullptr);
  if constexpr (side == Side::ServerSide) {
    doca_check(doca_ctx_get_user_data(doca_comch_server_as_ctx(doca_comch_server_get_server_ctx(c)), &user_data));
  } else if constexpr (side == Side::ClientSide) {
    doca_check(doca_ctx_get_user_data(doca_comch_client_as_ctx(doca_comch_client_get_client_ctx(c)), &user_data));
  } else {
    static_assert(false, "Wrong side!");
  }
  return user_data.ptr;
}

}  // namespace

namespace doca::comch::ctrl_path {

Endpoint::Endpoint(Device &dev_, naive::Buffers &buffers_, std::string_view name_)
    : dev(dev_),
      buffers(buffers_),
      side(dev_.run_on_dpu() ? Side::ServerSide : Side::ClientSide),
      name(std::move(name_)) {
  doca_check(doca_pe_create(&pe));
  if (side == Side::ServerSide) {
    doca_check(doca_comch_server_create(dev.dev, dev.rep, name.data(), &s));
  } else {
    doca_check(doca_comch_client_create(dev.dev, name.data(), &c));
  }
  doca_check(doca_comch_cap_get_max_msg_size(doca_dev_as_devinfo(dev.dev), &max_msg_size));
  doca_check(doca_comch_cap_get_max_recv_queue_size(doca_dev_as_devinfo(dev.dev), &recv_queue_size));
  TRACE("{} {}", max_msg_size, recv_queue_size);
  if (side == Side::ServerSide) {
    auto ctx = doca_comch_server_as_ctx(s);
    doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb<Side::ServerSide>));
    doca_check(doca_comch_server_task_send_set_conf(s, task_completion_cb, task_error_cb, recv_queue_size));
    doca_check(doca_comch_server_event_msg_recv_register(s, recv_event_cb<Side::ServerSide>));
    doca_check(doca_comch_server_event_connection_status_changed_register(s, connect_event_cb, disconnect_event_cb));
    doca_check(doca_comch_server_event_consumer_register(s, new_consumer_event_cb<Side::ServerSide>,
                                                         expired_consumer_event_cb<Side::ServerSide>));
    doca_check(doca_comch_server_set_max_msg_size(s, max_msg_size));
    doca_check(doca_comch_server_set_recv_queue_size(s, recv_queue_size));
    doca_check(doca_pe_connect_ctx(pe, ctx));
    doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
    doca_check(doca_ctx_start(ctx));
  } else {
    auto ctx = doca_comch_client_as_ctx(c);
    doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb<Side::ClientSide>));
    doca_check(doca_comch_client_task_send_set_conf(c, task_completion_cb, task_error_cb, recv_queue_size));
    doca_check(doca_comch_client_event_msg_recv_register(c, recv_event_cb<Side::ClientSide>));
    doca_check(doca_comch_client_event_consumer_register(c, new_consumer_event_cb<Side::ClientSide>,
                                                         expired_consumer_event_cb<Side::ClientSide>));
    doca_check(doca_comch_client_set_max_msg_size(c, max_msg_size));
    doca_check(doca_comch_client_set_recv_queue_size(c, recv_queue_size));
    doca_check(doca_pe_connect_ctx(pe, ctx));
    doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
    doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
  }
}

Endpoint::~Endpoint() {
  if (side == Side::ServerSide) {
    if (s != nullptr) {
      doca_check(doca_comch_server_destroy(s));
    }
  } else {
    if (c != nullptr) {
      doca_check(doca_comch_client_destroy(c));
    }
  }
  if (pe != nullptr) {
    doca_check(doca_pe_destroy(pe));
  }
}

bool Endpoint::progress() { return doca_pe_progress(pe); }

op_res_future_t Endpoint::post_recv(OpContext &ctx) {
  if (stopping()) {
    ctx.op_res.set_value(0);
    return ctx.op_res.get_future();
  }
  recv_ops_q.emplace_back(ctx);
  return ctx.op_res.get_future();
}

op_res_future_t Endpoint::post_send(OpContext &ctx) {
  doca_comch_task_send *task = nullptr;
  if (side == Side::ServerSide) {
    doca_check(doca_comch_server_task_send_alloc_init(s, conn, ctx.buf.data(), ctx.len, &task));
  } else {
    doca_check(doca_comch_client_task_send_alloc_init(c, conn, ctx.buf.data(), ctx.len, &task));
  }
  doca_task_set_user_data(doca_comch_task_send_as_task(task), doca_data(&ctx));
  doca_check(doca_task_submit(doca_comch_task_send_as_task(task)));
  return ctx.op_res.get_future();
}

template <Side side>
void Endpoint::state_change_cb(const doca_data ctx_user_data, doca_ctx *, doca_ctx_states prev_state,
                               doca_ctx_states next_state) {
  auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
  TRACE("DOCA Comch {} {} state change: {} -> {}", e->name, side, prev_state, next_state);
  switch (next_state) {
    case DOCA_CTX_STATE_IDLE: {
      e->shutdown();  // must progress to this state
    } break;
    case DOCA_CTX_STATE_STARTING: {
    } break;
    case DOCA_CTX_STATE_RUNNING: {
      if constexpr (side == Side::ClientSide) {
        doca_check(doca_comch_client_get_connection(e->c, &e->conn));
        e->run();
      }
    } break;
    case DOCA_CTX_STATE_STOPPING: {
    } break;
  }
}

void Endpoint::connect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *conn,
                                uint8_t success) {
  if (!success) {
    ERROR("Unsucceed connection");
  }
  auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection<Side::ServerSide>(conn));
  if (e->conn == nullptr) {
    e->conn = conn;
    e->run();
    TRACE("Establish connection of {}", e->name);
  } else {
    WARN("Only support one connection, ignored");
  }
}

void Endpoint::disconnect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *conn,
                                   uint8_t success) {
  if (!success) {
    ERROR("Unsucceed disconnection");
  }
  auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection<Side::ServerSide>(conn));
  if (e->conn == conn) {
    e->conn = nullptr;
    e->stop();
    doca_check_ext(doca_ctx_stop(doca_comch_server_as_ctx(e->s)), DOCA_ERROR_IN_PROGRESS);
    for (OpContext &op_ctx : e->recv_ops_q) {
      op_ctx.op_res.set_value(0);
    }  // notify all workers
    TRACE("Disconnection of {}", e->name);
  } else {
    WARN("Only support one connection, ignored");
  }
}

template <Side side>
void Endpoint::new_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t) {}
template <Side side>
void Endpoint::expired_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t) {}
void Endpoint::task_completion_cb(doca_comch_task_send *, doca_data task_user_data, doca_data) {
  auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
  ctx->op_res.set_value(ctx->len);
}
void Endpoint::task_error_cb(doca_comch_task_send *, doca_data task_user_data, doca_data) {
  auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
  ctx->op_res.set_value(0);
}
template <Side side>
void Endpoint::recv_event_cb(struct doca_comch_event_msg_recv *, uint8_t *buf, uint32_t len,
                             struct doca_comch_connection *conn) {
  Endpoint *e = nullptr;
  if constexpr (side == Side::ServerSide) {
    e = reinterpret_cast<Endpoint *>(get_user_data_from_connection<Side::ServerSide>(conn));
  } else if constexpr (side == Side::ClientSide) {
    e = reinterpret_cast<Endpoint *>(get_user_data_from_connection<Side::ClientSide>(conn));
  } else {
    static_unreachable;
  }
  assert(!e->recv_ops_q.empty());
  OpContext &op_ctx = e->recv_ops_q.front();
  assert(op_ctx.len >= len);
  memcpy(op_ctx.buf.data(), buf, len);
  op_ctx.op_res.set_value(len);
  e->recv_ops_q.pop_front();
}

}  // namespace doca::comch::ctrl_path

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
