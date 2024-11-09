#pragma once

#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_ctx.h>
#include <doca_pe.h>

#include "doca/check.hxx"
#include "doca/device.hxx"
#include "doca/simple_buffer.hxx"
#include "memory/simple_buffer.hxx"
#include "priv/context.hxx"
#include "priv/defs.hxx"
#include "priv/endpoint.hxx"

namespace doca {

inline static doca_ctx_states get_ctx_state(doca_ctx *ctx) {
  doca_ctx_states state;
  doca_check(doca_ctx_get_state(ctx, &state));
  return state;
}

}  // namespace doca

namespace doca::comch {

template <Side side>
class Endpoint : public EndpointBase {
  template <Side>
  friend class ConnectionHandle;

 public:
  Endpoint(Device &dev_, doca::Buffers &send_buffers_, doca::Buffers &recv_buffers_)
      : dev(dev_), send_buffers(send_buffers_), recv_buffers(recv_buffers_) {
    doca_check(doca_pe_create(&pe));
  }

  ~Endpoint() {
    if (pro != nullptr) {
      doca_check(doca_comch_producer_destroy(pro));
    }
    if (con != nullptr) {
      doca_check(doca_comch_consumer_destroy(con));
    }
    if (pe != nullptr) {
      doca_check(doca_pe_destroy(pe));
    }
  }

  bool progress() { return doca_pe_progress(pe); }

  op_res_future_t post_recv(OpContext &ctx) {
    if (consumer_stopped()) {
      ctx.op_res.set_value(0);
      return ctx.op_res.get_future();
    }
    doca_comch_consumer_task_post_recv *task = nullptr;
    auto &buf = static_cast<doca::BorrowedBuffer &>(ctx.buf);

    void *head = nullptr;
    void *data = nullptr;
    size_t len = 0;
    size_t data_len = 0;
    doca_check(doca_buf_get_head(buf.buf, &head));
    doca_check(doca_buf_get_data(buf.buf, &data));
    doca_check(doca_buf_get_len(buf.buf, &len));
    doca_check(doca_buf_get_data_len(buf.buf, &data_len));
    DEBUG("ctx {} buf.base {} buf.len {} head {} len {} data {} data_len {} remote_consumer_id {}", (void *)&ctx,
          (void *)buf.base, buf.len, head, len, data, data_len, consumer_id);

    doca_check(doca_comch_consumer_task_post_recv_alloc_init(con, buf.buf, &task));
    doca_task_set_user_data(doca_comch_consumer_task_post_recv_as_task(task), doca_data(&ctx));
    doca_check(doca_task_submit(doca_comch_consumer_task_post_recv_as_task(task)));
    return ctx.op_res.get_future();
  }

  op_res_future_t post_send(OpContext &ctx) {
    if (producer_stopped() || consumer_id == 0) {
      ctx.op_res.set_value(0);
      return ctx.op_res.get_future();
    }
    doca_comch_producer_task_send *task = nullptr;
    auto &buf = static_cast<doca::BorrowedBuffer &>(ctx.buf);
    doca_check(doca_buf_set_data_len(buf.buf, ctx.len));

    void *head = nullptr;
    void *data = nullptr;
    size_t len = 0;
    size_t data_len = 0;
    doca_check(doca_buf_get_head(buf.buf, &head));
    doca_check(doca_buf_get_data(buf.buf, &data));
    doca_check(doca_buf_get_len(buf.buf, &len));
    doca_check(doca_buf_get_data_len(buf.buf, &data_len));
    DEBUG("ctx {} buf.base {} buf.len {} head {} len {} data {} data_len {} remote_consumer_id {}", (void *)&ctx,
          (void *)buf.base, buf.len, head, len, data, data_len, consumer_id);

    doca_check(doca_comch_producer_task_send_alloc_init(pro, buf.buf, nullptr, 0, consumer_id, &task));
    doca_task_set_user_data(doca_comch_producer_task_send_as_task(task), doca_data(&ctx));
    doca_check(doca_task_submit(doca_comch_producer_task_send_as_task(task)));
    return ctx.op_res.get_future();
  }

 protected:
  void prepare(doca_comch_connection *conn_) {
    conn = conn_;
    {
      doca_check(doca_comch_consumer_create(conn, recv_buffers.mmap, &con));
      auto ctx = doca_comch_consumer_as_ctx(con);
      doca_check(doca_pe_connect_ctx(pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, consumer_state_change_cb));
      doca_check(
          doca_comch_consumer_task_post_recv_set_conf(con, post_recv_cb, post_recv_err_cb, recv_buffers.n_elements()));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check_ext(doca_ctx_start(ctx), DOCA_ERROR_IN_PROGRESS);
    }
    {
      doca_check(doca_comch_producer_create(conn, &pro));
      auto ctx = doca_comch_producer_as_ctx(pro);
      doca_check(doca_pe_connect_ctx(pe, ctx));
      doca_check(doca_ctx_set_state_changed_cb(ctx, producer_state_change_cb));
      doca_check(
          doca_comch_producer_task_send_set_conf(pro, post_send_cb, post_send_err_cb, send_buffers.n_elements()));
      doca_check(doca_ctx_set_user_data(ctx, doca_data(this)));
      doca_check(doca_ctx_start(ctx));
    }
    doca_check(doca_comch_consumer_get_id(con, &consumer_id));
    EndpointBase::prepare();
  }

  void run(uint32_t remote_consumer_id_) {
    assert(consumer_id == remote_consumer_id_);
    EndpointBase::run();
  }

  void stop() {
    doca_check(doca_ctx_stop(doca_comch_producer_as_ctx(pro)));
    doca_check_ext(doca_ctx_stop(doca_comch_consumer_as_ctx(con)), DOCA_ERROR_IN_PROGRESS);
    EndpointBase::stop();
  }

 private:
  static void post_send_cb(doca_comch_producer_task_send *task, doca_data task_user_data, doca_data) {
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    [[maybe_unused]] auto &buf = reinterpret_cast<doca::BorrowedBuffer &>(ctx->buf);
    TRACE("One producer send task done, {}", *ctx);
    ctx->op_res.set_value(ctx->len);
    doca_task_free(doca_comch_producer_task_send_as_task(task));
  }

  static void post_send_err_cb(doca_comch_producer_task_send *task, doca_data task_user_data, doca_data) {
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    [[maybe_unused]] auto error = doca_task_get_status(doca_comch_producer_task_send_as_task(task));
    TRACE("One producer send task error, {}, result: {}", *ctx, doca_error_get_descr(error));
    ctx->op_res.set_value(0);
    doca_task_free(doca_comch_producer_task_send_as_task(task));
  }

  static void post_recv_cb(doca_comch_consumer_task_post_recv *task, doca_data task_user_data, doca_data) {
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    TRACE("One consumer recv task done, {}", *ctx);
    size_t data_len = 0;
    doca_check(doca_buf_get_data_len(doca_comch_consumer_task_post_recv_get_buf(task), &data_len));
    ctx->op_res.set_value(data_len);
    doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
  }

  static void post_recv_err_cb(doca_comch_consumer_task_post_recv *task, doca_data task_user_data, doca_data) {
    auto ctx = reinterpret_cast<OpContext *>(task_user_data.ptr);
    [[maybe_unused]] auto error = doca_task_get_status(doca_comch_consumer_task_post_recv_as_task(task));
    TRACE("One consumer recv task error, {}, result: {}", *ctx, doca_error_get_name(error));
    ctx->op_res.set_value(0);
    doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
  }

  static void producer_state_change_cb(const doca_data ctx_user_data, doca_ctx *,
                                       [[maybe_unused]] doca_ctx_states prev_state,
                                       [[maybe_unused]] doca_ctx_states next_state) {
    auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    INFO("DOCA Comch producer {} state change: {} -> {}", e->consumer_id, side, prev_state, next_state);
  }

  static void consumer_state_change_cb(const doca_data ctx_user_data, doca_ctx *,
                                       [[maybe_unused]] doca_ctx_states prev_state,
                                       [[maybe_unused]] doca_ctx_states next_state) {
    auto e = reinterpret_cast<Endpoint *>(ctx_user_data.ptr);
    INFO("DOCA Comch consumer {} state change: {} -> {}", e->consumer_id, side, prev_state, next_state);
    switch (next_state) {
      case DOCA_CTX_STATE_IDLE: {
        e->shutdown();
      } break;
      case DOCA_CTX_STATE_STARTING:
      case DOCA_CTX_STATE_RUNNING:
      case DOCA_CTX_STATE_STOPPING:
    }
  }

  doca_ctx_states consumer_state() { return get_ctx_state(doca_comch_consumer_as_ctx(con)); }

  doca_ctx_states producer_state() { return get_ctx_state(doca_comch_producer_as_ctx(pro)); }

  bool consumer_running() { return consumer_state() == DOCA_CTX_STATE_RUNNING; }

  bool consumer_stopped() { return consumer_state() == DOCA_CTX_STATE_IDLE; }

  bool producer_running() { return producer_state() == DOCA_CTX_STATE_RUNNING; }

  bool producer_stopped() { return producer_state() == DOCA_CTX_STATE_IDLE; }

 private:
  Device &dev;
  doca::Buffers &send_buffers;
  doca::Buffers &recv_buffers;
  doca_pe *pe = nullptr;
  doca_comch_connection *conn = nullptr;
  doca_comch_consumer *con = nullptr;
  doca_comch_producer *pro = nullptr;
  uint32_t consumer_id = 0;
};

}  // namespace doca::comch

// clang-format off
EnumFormatter(doca_ctx_states,
    [DOCA_CTX_STATE_IDLE]     = "Idle",
    [DOCA_CTX_STATE_STARTING] = "Starting",
    [DOCA_CTX_STATE_RUNNING]  = "Running",
    [DOCA_CTX_STATE_STOPPING] = "Stopping",
);
// clang-format on

template <>
struct std::formatter<OpContext> : std::formatter<std::string> {
  template <typename Context>
  Context::iterator format(const OpContext &ctx, Context out) const {
    return std::formatter<std::string>::format(
        std::format("{} op, buf: {}, buf_len: {}, data_len: {}", ctx.op, reinterpret_cast<const void *>(ctx.buf.data()),
                    ctx.buf.size(), ctx.len),
        out);
  }
};
