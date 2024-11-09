#pragma once

#include <doca_comch.h>
#include <doca_pe.h>

#include <algorithm>
#include <functional>
#include <glaze/glaze.hpp>
#include <string>
#include <thread>
#include <vector>

#include "doca/check.hxx"
#include "doca/device.hxx"
#include "priv/defs.hxx"
#include "priv/doca_comch/endpoint.hxx"
#include "util/logger.hxx"
#include "util/unreachable.hxx"

using namespace std::chrono_literals;

namespace doca::comch {

struct ConnectionParam {
  std::string name;
};

template <Side s>
class Endpoint;

template <Side side>
class ConnectionHandle {
  using Endpoint = Endpoint<side>;
  using EndpointRef = std::reference_wrapper<Endpoint>;
  using EndpointRefs = std::vector<EndpointRef>;

 public:
  ConnectionHandle(Device& dev_, const ConnectionParam& param_) : dev(dev_), param(param_) {
    auto caps = dev.probe_comch_params();

    INFO("Comch capability:\n{}", glz::write<glz::opts{.prettify = true}>(caps).value_or("Unexpected!"));

    doca_check(doca_pe_create(&pe));
    if constexpr (side == Side::ServerSide) {
      doca_check(doca_comch_server_create(dev.dev, dev.rep, param.name.data(), &s));
    } else if constexpr (side == Side::ClientSide) {
      doca_check(doca_comch_client_create(dev.dev, param.name.data(), &c));
    } else {
      static_unreachable;
    }

    if constexpr (side == Side::ServerSide) {
      auto ctx = doca_comch_server_as_ctx(s);
      doca_check(doca_comch_server_task_send_set_conf(s, task_completion_cb, task_error_cb,
                                                      caps.ctrl_path.max_recv_queue_size));
      doca_check(doca_comch_server_event_msg_recv_register(s, recv_event_cb));
      doca_check(doca_comch_server_event_connection_status_changed_register(s, connect_event_cb, disconnect_event_cb));
      doca_check(doca_comch_server_event_consumer_register(s, new_consumer_event_cb, expired_consumer_event_cb));
      doca_check(doca_comch_server_set_max_msg_size(s, caps.ctrl_path.max_msg_size));
      doca_check(doca_comch_server_set_recv_queue_size(s, caps.ctrl_path.max_recv_queue_size));
      doca_check(doca_pe_connect_ctx(pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_ctx_start(ctx));
    } else if constexpr (side == Side::ClientSide) {
      auto ctx = doca_comch_client_as_ctx(c);
      doca_check(doca_comch_client_task_send_set_conf(c, task_completion_cb, task_error_cb,
                                                      caps.ctrl_path.max_recv_queue_size));
      doca_check(doca_comch_client_event_msg_recv_register(c, recv_event_cb));
      doca_check(doca_comch_client_event_consumer_register(c, new_consumer_event_cb, expired_consumer_event_cb));
      doca_check(doca_comch_client_set_max_msg_size(c, caps.ctrl_path.max_msg_size));
      doca_check(doca_comch_client_set_recv_queue_size(c, caps.ctrl_path.max_recv_queue_size));
      doca_check(doca_pe_connect_ctx(pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, state_change_cb));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
    } else {
      static_unreachable;
    }

    progress_until([this]() { return established() && conn != nullptr; });
  }

  ~ConnectionHandle() {
    if constexpr (side == Side::ClientSide) {
      doca_check_ext(doca_ctx_stop(doca_comch_client_as_ctx(c)), DOCA_ERROR_IN_PROGRESS);
    } else if constexpr (side == Side::ServerSide) {
      doca_check_ext(doca_ctx_stop(doca_comch_server_as_ctx(s)), DOCA_ERROR_IN_PROGRESS);
    } else {
      static_unreachable;
    }

    progress_until([this]() { return disconnected(); });

    if constexpr (side == Side::ServerSide) {
      if (s != nullptr) {
        doca_check(doca_comch_server_destroy(s));
      }
    } else if constexpr (side == Side::ClientSide) {
      if (c != nullptr) {
        doca_check(doca_comch_client_destroy(c));
      }
    }
    if (pe != nullptr) {
      doca_check(doca_pe_destroy(pe));
    }
  }

  ConnectionHandle& associate(Endpoint& e) {
    pending_endpoints.emplace_back(e);
    return *this;
  }

  ConnectionHandle& associate(EndpointRefs&& es) {
    pending_endpoints.insert(pending_endpoints.end(), std::make_move_iterator(es.begin()),
                             std::make_move_iterator(es.end()));
    return *this;
  }

  void listen_and_accept() {
    for_each_endpoint([this](Endpoint& e) { e.prepare(conn); });
    progress_all_until([](Endpoint& e) { return e.running(); });
  }

  void wait_for_disconnect() {
    // for another thread
    progress_all_until([](Endpoint& e) { return e.exited(); });
  }

  void connect() {
    for_each_endpoint([this](Endpoint& e) { e.prepare(conn); });
    progress_all_until([](Endpoint& e) { return e.running(); });
  }

  void disconnect() {
    for_each_endpoint([](Endpoint& e) { e.stop(); });
    progress_all_until([](Endpoint& e) { return e.exited(); });
  }

 private:
  template <typename Fn>
  void for_each_endpoint(Fn&& fn) {
    std::ranges::for_each(pending_endpoints, fn);
  }
  void progress_all_until(std::function<bool(Endpoint&)>&& predictor) {
    while (true) {
      uint32_t n_satisfied = 0;
      for_each_endpoint([&n_satisfied, &predictor](Endpoint& e) {
        if (!predictor(e)) {
          e.progress();
        } else {
          n_satisfied++;
        }
      });
      progress();
      if (n_satisfied == pending_endpoints.size()) {
        return;
      } else {
        std::this_thread::sleep_for(10us);
      }
    }
  }

  bool progress() { return doca_pe_progress(pe); }

  void progress_until(std::function<bool(void)>&& predictor) {
    while (!predictor()) {
      if (!progress()) {
        std::this_thread::sleep_for(10us);
      }
    }
  }

  doca_ctx_states client_state()
    requires(side == Side::ClientSide)
  {
    return get_ctx_state(doca_comch_client_as_ctx(c));
  }

  doca_ctx_states server_state()
    requires(side == Side::ServerSide)
  {
    return get_ctx_state(doca_comch_server_as_ctx(s));
  }

  bool established() {
    if constexpr (side == Side::ClientSide) {
      return client_state() == DOCA_CTX_STATE_RUNNING;
    } else if constexpr (side == Side::ServerSide) {
      return server_state() == DOCA_CTX_STATE_RUNNING;
    } else {
      static_unreachable;
    }
  }

  bool disconnected() {
    if constexpr (side == Side::ClientSide) {
      return client_state() == DOCA_CTX_STATE_IDLE;
    } else if constexpr (side == Side::ServerSide) {
      return server_state() == DOCA_CTX_STATE_IDLE;
    } else {
      static_unreachable;
    }
  }

  static void state_change_cb(const doca_data ctx_user_data, doca_ctx*, [[maybe_unused]] doca_ctx_states prev_state,
                              [[maybe_unused]] doca_ctx_states next_state) {
    auto e = reinterpret_cast<ConnectionHandle*>(ctx_user_data.ptr);
    INFO("DOCA Comch {} {} state change: {} -> {}", e->param.name, side, prev_state, next_state);
  }

  static void connect_event_cb(doca_comch_event_connection_status_changed*, doca_comch_connection* conn,
                               uint8_t success) {
    if (!success) {
      ERROR("Unsucceed connection");
    }
    auto e = reinterpret_cast<ConnectionHandle*>(get_user_data_from_connection(conn));
    if (e->conn == nullptr) {
      e->conn = conn;
      INFO("Establish ctrl path of {}", e->param.name);
    } else {
      WARN("Only support one connection, ignored");
    }
  }

  static void disconnect_event_cb(doca_comch_event_connection_status_changed*, doca_comch_connection* conn,
                                  uint8_t success) {
    if (!success) {
      ERROR("Unsucceed disconnection");
    }
    auto e = reinterpret_cast<ConnectionHandle*>(get_user_data_from_connection(conn));
    if (e->conn == conn) {
      e->conn = nullptr;
      INFO("Disconnect ctrl path of {}", e->param.name);
    } else {
      WARN("Only support one connection, ignore");
    }
  }

  static void new_consumer_event_cb(doca_comch_event_consumer*, doca_comch_connection* conn,
                                    uint32_t remote_consumer_id) {
    auto e = reinterpret_cast<ConnectionHandle*>(get_user_data_from_connection(conn));
    if (conn != e->conn) {
      WARN("Only support one connection, ignore");
    } else {
      for (Endpoint& endpoint : e->pending_endpoints) {
        if (endpoint.consumer_id == remote_consumer_id) {
          endpoint.run(remote_consumer_id);
          INFO("Establish data path of {}:{}", e->param.name, remote_consumer_id);
        }
      }
    }
  }

  static void expired_consumer_event_cb(doca_comch_event_consumer*, doca_comch_connection* conn,
                                        uint32_t remote_consumer_id) {
    auto e = reinterpret_cast<ConnectionHandle*>(get_user_data_from_connection(conn));
    if (conn != e->conn) {
      WARN("Only support one connection, ignore");
    } else {
      if constexpr (side == Side::ServerSide) {
        for (Endpoint& endpoint : e->pending_endpoints) {
          if (endpoint.consumer_id == remote_consumer_id) {
            endpoint.stop();
            INFO("Disconnect data path of {}:{}", e->param.name, remote_consumer_id);
          }
        }
      }
    }
  }

  static void task_completion_cb(doca_comch_task_send*, doca_data, doca_data) {
    // auto ctx = reinterpret_cast<OpContext*>(task_user_data.ptr);
    // ctx->op_res.set_value(ctx->len);
    // doca_task_free(doca_comch_task_send_as_task(task));
  }

  static void task_error_cb(doca_comch_task_send*, doca_data, doca_data) {
    // auto ctx = reinterpret_cast<OpContext*>(task_user_data.ptr);
    // auto error = doca_task_get_status(doca_comch_task_send_as_task(task));
    // WARN("Ctrl path send task error: {}", doca_error_get_descr(error));
    // ctx->op_res.set_value(0);
    // doca_task_free(doca_comch_task_send_as_task(task));
  }

  static void recv_event_cb(doca_comch_event_msg_recv*, uint8_t*, uint32_t, doca_comch_connection*) {
    // Endpoint* e = nullptr;
    // if constexpr (side == Side::ServerSide) {
    //   e = reinterpret_cast<Endpoint*>(get_user_data_from_connection(conn));
    // } else if constexpr (side == Side::ClientSide) {
    //   e = reinterpret_cast<Endpoint*>(get_user_data_from_connection(conn));
    // } else {
    //   static_unreachable;
    // }
    // assert(!e->recv_ops_q.empty());
    // OpContext& op_ctx = e->recv_ops_q.front();
    // assert(op_ctx.len >= len);
    // memcpy(op_ctx.buf.data(), buf, len);
    // op_ctx.op_res.set_value(len);
    // e->recv_ops_q.pop_front();
  }

  static void* get_user_data_from_connection(doca_comch_connection* conn) {
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

  Device& dev;
  const ConnectionParam& param;
  doca_pe* pe;
  union {
    doca_comch_server* s = nullptr;
    doca_comch_client* c;
  };
  doca_comch_connection* conn = nullptr;
  EndpointRefs pending_endpoints;
};

}  // namespace doca::comch
