#pragma once

#include <doca_comch.h>
#include <doca_ctx.h>
#include <doca_pe.h>

#include "doca/device.hxx"
#include "doca/simple_buffer.hxx"
#include "priv/common.hxx"

namespace doca::comch::ctrl_path {

class Endpoint : public EndpointBase {
  friend class ConnectionHandle;

 public:
  Endpoint(Device &dev_, Buffers &buffers_, std::string name_);

  ~Endpoint();

  void prepare();
  void run();
  void stop();

  bool progress();

  op_res_future_t post_recv(OpContext &ctx);
  op_res_future_t post_send(OpContext &ctx);

  template <Side side>
  static void state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);
  static void connect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t);
  static void disconnect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t);
  template <Side side>
  static void new_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);
  template <Side side>
  static void expired_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);
  static void task_completion_cb(doca_comch_task_send *, doca_data, doca_data);
  static void task_error_cb(doca_comch_task_send *, doca_data, doca_data);
  template <Side side>
  static void recv_event_cb(struct doca_comch_event_msg_recv *, uint8_t *, uint32_t, struct doca_comch_connection *);

 private:
  Device &dev;
  Buffers &buffers;
  Side side;
  std::string name;
  uint32_t max_msg_size = -1;
  uint32_t recv_queue_size = -1;
  doca_pe *pe = nullptr;
  union {
    doca_comch_server *s = nullptr;
    doca_comch_client *c;
  };
  doca_comch_connection *conn = nullptr;
  std::list<std::reference_wrapper<OpContext>> recv_ops_q;
};

}  // namespace doca::comch::ctrl_path
