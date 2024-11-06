#pragma once

#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_ctx.h>
#include <doca_pe.h>

#include <glaze/glaze.hpp>
#include <list>

#include "doca/check.hxx"
#include "doca/device.hxx"
#include "doca/simple_buffer.hxx"
#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "util/unreachable.hxx"

namespace doca {}  // namespace doca

namespace doca::comch {

template <Side side>
class Endpoint : public EndpointBase {
  template <Side>
  friend class ConnectionHandle;

 public:
  Endpoint(Device &dev_, doca::Buffers &buffers_, doca::Buffers &bulk_buffers_, std::string_view name_)
      : dev(dev_), buffers(buffers_), bulk_buffers(bulk_buffers_), name(name_) {
    auto caps = dev_.probe_comch_params();

    if (caps.ctrl_path.max_msg_size < buffers.piece_size()) {
      die("Device max rpc message size: {}", caps.ctrl_path.max_msg_size);
    }
    if (caps.ctrl_path.max_recv_queue_size < buffers.n_elements()) {
      die("Device max recv queue depth: {}", caps.ctrl_path.max_recv_queue_size);
    }

    INFO("Comch capability:\n{}", glz::write<glz::opts{.prettify = true}>(caps).value_or("Unexpected!"));

    auto recv_queue_size = std::min(caps.ctrl_path.max_recv_queue_size, (uint32_t)buffers.n_elements());
    auto msg_size = std::min(caps.ctrl_path.max_msg_size, (uint32_t)buffers.piece_size());

    doca_check(doca_pe_create(&cp_pe));
    if constexpr (side == Side::ServerSide) {
      doca_check(doca_comch_server_create(dev.dev, dev.rep, name.data(), &s));
    } else if constexpr (side == Side::ClientSide) {
      doca_check(doca_comch_client_create(dev.dev, name.data(), &c));
    } else {
      static_unreachable;
    }

    if constexpr (side == Side::ServerSide) {
      auto ctx = doca_comch_server_as_ctx(s);
      doca_check(doca_comch_server_task_send_set_conf(s, task_completion_cb, task_error_cb, recv_queue_size));
      doca_check(doca_comch_server_event_msg_recv_register(s, recv_event_cb));
      doca_check(doca_comch_server_event_connection_status_changed_register(s, connect_event_cb, disconnect_event_cb));
      doca_check(doca_comch_server_event_consumer_register(s, new_consumer_event_cb, expired_consumer_event_cb));
      doca_check(doca_comch_server_set_max_msg_size(s, msg_size));
      doca_check(doca_comch_server_set_recv_queue_size(s, recv_queue_size));
      doca_check(doca_pe_connect_ctx(cp_pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_ctx_start(ctx));
    } else if constexpr (side == Side::ClientSide) {
      auto ctx = doca_comch_client_as_ctx(c);
      doca_check(doca_comch_client_task_send_set_conf(c, task_completion_cb, task_error_cb, recv_queue_size));
      doca_check(doca_comch_client_event_msg_recv_register(c, recv_event_cb));
      doca_check(doca_comch_client_event_consumer_register(c, new_consumer_event_cb, expired_consumer_event_cb));
      doca_check(doca_comch_client_set_max_msg_size(c, msg_size));
      doca_check(doca_comch_client_set_recv_queue_size(c, recv_queue_size));
      doca_check(doca_pe_connect_ctx(cp_pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
    } else {
      static_unreachable;
    }
  }

  ~Endpoint() {
    if constexpr (side == Side::ServerSide) {
      if (s != nullptr) {
        doca_check(doca_comch_server_destroy(s));
      }
    } else if constexpr (side == Side::ClientSide) {
      if (c != nullptr) {
        doca_check(doca_comch_client_destroy(c));
      }
    }
    if (cp_pe != nullptr) {
      doca_check(doca_pe_destroy(cp_pe));
    }
  }

  bool progress() { return doca_pe_progress(cp_pe); }

  op_res_future_t post_recv(OpContext &ctx) {
    doca_comch_consumer_task_post_recv *task = nullptr;
    auto &buf = static_cast<doca::BorrowedBuffer &>(ctx.buf);
    doca_check(doca_comch_consumer_task_post_recv_alloc_init(con, buf.buf, &task));
    doca_task_set_user_data(doca_comch_consumer_task_post_recv_as_task(task), doca_data(&ctx));
    doca_check(doca_task_submit(doca_comch_consumer_task_post_recv_as_task(task)));
    return ctx.op_res.get_future();
  }

  op_res_future_t post_send(OpContext &ctx) {
    doca_comch_producer_task_send *task = nullptr;
    auto &buf = static_cast<doca::BorrowedBuffer &>(ctx.buf);
    doca_check(doca_buf_set_data_len(buf.buf, ctx.len));
    doca_check(doca_comch_producer_task_send_alloc_init(pro, buf.buf, reinterpret_cast<uint8_t *>(ctx.len),
                                                        sizeof(ctx.len), remote_consumer_id, &task));
    doca_task_set_user_data(doca_comch_producer_task_send_as_task(task), doca_data(&ctx));
    doca_check(doca_task_try_submit(doca_comch_producer_task_send_as_task(task)));
    return ctx.op_res.get_future();
  }

  op_res_future_t cp_post_recv(OpContext &ctx) {
    recv_ops_q.emplace_back(ctx);
    return ctx.op_res.get_future();
  }

  op_res_future_t cp_post_send(OpContext &ctx) {
    doca_comch_task_send *task = nullptr;
    if constexpr (side == Side::ServerSide) {
      doca_check(doca_comch_server_task_send_alloc_init(s, conn, ctx.buf.data(), ctx.len, &task));
    } else if constexpr (side == Side::ClientSide) {
      doca_check(doca_comch_client_task_send_alloc_init(c, conn, ctx.buf.data(), ctx.len, &task));
    } else {
      static_unreachable;
    }
    doca_task_set_user_data(doca_comch_task_send_as_task(task), doca_data(&ctx));
    doca_check(doca_task_submit(doca_comch_task_send_as_task(task)));
    return ctx.op_res.get_future();
  }

 protected:
  void prepare() {
    assert(conn != nullptr);
    {
      doca_check(doca_comch_producer_create(conn, &pro));
      auto ctx = doca_comch_producer_as_ctx(pro);
      doca_check(doca_pe_connect_ctx(cp_pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, producer_state_change_cb));
      doca_check(doca_comch_producer_task_send_set_conf(pro, post_send_cb<CompCbType::OK>,
                                                        post_send_cb<CompCbType::ERROR>, buffers.n_elements()));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_ctx_start(ctx));
    }
    {
      doca_check(doca_comch_consumer_create(conn, bulk_buffers.mmap, &con));
      auto ctx = doca_comch_consumer_as_ctx(con);
      doca_check(doca_pe_connect_ctx(cp_pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, consumer_state_change_cb));
      doca_check(doca_comch_consumer_task_post_recv_set_conf(
          con, post_recv_cb<CompCbType::OK>, post_recv_cb<CompCbType::ERROR>, bulk_buffers.n_elements()));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
    }

    // doca_check(doca_comch_connection_set_user_data(conn, doca_data(this)));
    EndpointBase::prepare();
  }

  void stop() {
    doca_check(doca_ctx_stop(doca_comch_consumer_as_ctx(con)));
    doca_check(doca_ctx_stop(doca_comch_producer_as_ctx(pro)));
    EndpointBase::stop();
  }

 private:
  static void state_change_cb(const doca_data ctx_user_data, doca_ctx *, doca_ctx_states prev_state,
                              doca_ctx_states next_state) {
    auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    DEBUG("DOCA Comch {} {} state change: {} -> {}", e->name, side, prev_state, next_state);
    switch (next_state) {
      case DOCA_CTX_STATE_IDLE: {
        e->shutdown();  // must progress to this state
      } break;
      case DOCA_CTX_STATE_STARTING: {
      } break;
      case DOCA_CTX_STATE_RUNNING: {
        if constexpr (side == Side::ClientSide) {
          doca_check(doca_comch_client_get_connection(e->c, &e->conn));
        }
      } break;
      case DOCA_CTX_STATE_STOPPING: {
      } break;
    }
  }

  static void connect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *conn,
                               uint8_t success) {
    if (!success) {
      ERROR("Unsucceed connection");
    }
    auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection(conn));
    if (e->conn == nullptr) {
      e->conn = conn;
      DEBUG("Establish connection of {}", e->name);
    } else {
      WARN("Only support one connection, ignored");
    }
  }

  static void disconnect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *conn,
                                  uint8_t success) {
    if (!success) {
      ERROR("Unsucceed disconnection");
    }
    auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection(conn));
    if (e->conn == conn) {
      e->conn = nullptr;
      doca_check_ext(doca_ctx_stop(doca_comch_server_as_ctx(e->s)), DOCA_ERROR_IN_PROGRESS);
      for (OpContext &op_ctx : e->recv_ops_q) {
        op_ctx.op_res.set_value(0);
      }  // notify all workers
      DEBUG("Disconnection of {}", e->name);
    } else {
      WARN("Only support one connection, ignore");
    }
  }

  static void new_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *conn,
                                    uint32_t remote_consumer_id) {
    auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection(conn));
    if (e->remote_consumer_id == 0) {
      e->remote_consumer_id = remote_consumer_id;
    } else {
      WARN("Only support one connection, ignore");
    }
  }

  static void expired_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *conn,
                                        uint32_t remote_consumer_id) {
    auto e = reinterpret_cast<Endpoint *>(get_user_data_from_connection(conn));
    if (remote_consumer_id == e->remote_consumer_id) {
      e->remote_consumer_id = 0;
      if constexpr (side == Side::ServerSide) {
        e->stop();
      } else if constexpr (side == Side::ClientSide) {
        doca_check_ext(doca_ctx_stop(doca_comch_client_as_ctx(e->c)), DOCA_ERROR_IN_PROGRESS)
      } else {
        static_unreachable;
      }
    } else {
      WARN("Only support one connection, ignore");
    }
  }

  static void task_completion_cb(doca_comch_task_send *task, doca_data task_user_data, doca_data) {
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    ctx->op_res.set_value(ctx->len);
    doca_task_free(doca_comch_task_send_as_task(task));
  }

  static void task_error_cb(doca_comch_task_send *task, doca_data task_user_data, doca_data) {
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    ctx->op_res.set_value(0);
    doca_task_free(doca_comch_task_send_as_task(task));
  }

  static void recv_event_cb(struct doca_comch_event_msg_recv *, uint8_t *buf, uint32_t len,
                            struct doca_comch_connection *conn) {
    Endpoint *e = nullptr;
    if constexpr (side == Side::ServerSide) {
      e = reinterpret_cast<Endpoint *>(get_user_data_from_connection(conn));
    } else if constexpr (side == Side::ClientSide) {
      e = reinterpret_cast<Endpoint *>(get_user_data_from_connection(conn));
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

  enum class CompCbType {
    OK,
    ERROR,
  };

  template <CompCbType type>
  static void post_send_cb(struct doca_comch_producer_task_send *task, union doca_data task_user_data,
                           union doca_data /*ctx_user_data*/) {
    // auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    if constexpr (type == CompCbType::OK) {
      ctx->op_res.set_value(ctx->len);
    } else if constexpr (type == CompCbType::ERROR) {
      ctx->op_res.set_value(0);
    } else {
      static_unreachable;
    }
    doca_task_free(doca_comch_producer_task_send_as_task(task));
  }

  template <CompCbType type>
  static void post_recv_cb(struct doca_comch_consumer_task_post_recv *task, union doca_data task_user_data,
                           union doca_data /*ctx_user_data*/) {
    // auto endpoint = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    if constexpr (type == CompCbType::OK) {
      // size_t len = *reinterpret_cast<const size_t *>(doca_comch_consumer_task_post_recv_get_imm_data(task));
      ctx->op_res.set_value(ctx->len);
    } else if constexpr (type == CompCbType::ERROR) {
      ctx->op_res.set_value(0);
    } else {
      static_unreachable;
    }
    doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
  }

  static void producer_state_change_cb(const doca_data ctx_user_data, doca_ctx *, doca_ctx_states prev_state,
                                       doca_ctx_states next_state) {
    auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    DEBUG("DOCA Comch {} {} producer state change: {} -> {}", e->name, side, prev_state, next_state);
    switch (next_state) {
      case DOCA_CTX_STATE_IDLE: {
        doca_check(doca_comch_producer_destroy(e->pro));
        e->pro = nullptr;
      } break;
      case DOCA_CTX_STATE_STARTING: {
      } break;
      case DOCA_CTX_STATE_RUNNING: {
      } break;
      case DOCA_CTX_STATE_STOPPING: {
      } break;
    }
  }

  static void consumer_state_change_cb(const doca_data ctx_user_data, doca_ctx *, doca_ctx_states prev_state,
                                       doca_ctx_states next_state) {
    auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    DEBUG("DOCA Comch {} {} consumer state change: {} -> {}", e->name, side, prev_state, next_state);
    switch (next_state) {
      case DOCA_CTX_STATE_IDLE: {
        doca_check(doca_comch_consumer_destroy(e->con));
        e->con = nullptr;
      } break;
      case DOCA_CTX_STATE_STARTING: {
      } break;
      case DOCA_CTX_STATE_RUNNING: {
        e->run();
      } break;
      case DOCA_CTX_STATE_STOPPING: {
      } break;
    }
  }

  static void *get_user_data_from_connection(doca_comch_connection *conn) {
    doca_data user_data(nullptr);
    if constexpr (side == Side::ServerSide) {
      doca_check(doca_ctx_get_user_data(doca_comch_server_as_ctx(doca_comch_server_get_server_ctx(conn)), &user_data));
    } else if constexpr (side == Side::ClientSide) {
      doca_check(doca_ctx_get_user_data(doca_comch_client_as_ctx(doca_comch_client_get_client_ctx(conn)), &user_data));
    } else {
      static_unreachable;
    }
    return user_data.ptr;
  }

 private:
  Device &dev;
  doca::Buffers &buffers;
  doca::Buffers &bulk_buffers;
  std::string name;
  doca_pe *cp_pe = nullptr;
  union {
    doca_comch_server *s = nullptr;
    doca_comch_client *c;
  };
  doca_comch_connection *conn = nullptr;
  doca_comch_consumer *con = nullptr;
  doca_comch_producer *pro = nullptr;
  uint32_t remote_consumer_id = 0;
  std::list<std::reference_wrapper<OpContext>> recv_ops_q;
};

}  // namespace doca::comch

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
