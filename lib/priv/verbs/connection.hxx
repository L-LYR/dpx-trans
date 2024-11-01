#pragma once

#include <rdma/rdma_cma.h>

#include "priv/common.hxx"
#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

namespace verbs {

class Endpoint;

struct EventChannel : Noncopyable, Nonmovable {
  explicit EventChannel(rdma_event_channel* p_);

  EventChannel();
  ~EventChannel();
  rdma_cm_event* get_event();
  rdma_cm_event* wait(rdma_cm_event_type expected);
  void ack(rdma_cm_event* e);
  void wait_and_ack(rdma_cm_event_type expected);

  rdma_event_channel* p = nullptr;
};

class ConnectionHandle : public ConnectionHandleBase<ConnectionHandle, Endpoint> {
 public:
  ConnectionHandle(const ConnectionParam& param);
  ~ConnectionHandle();

 public:
  void listen_and_accept();
  void wait_for_disconnect();

  void connect();
  void disconnect();

 private:
  EventChannel c;
  rdma_cm_id* id = nullptr;
};

}  // namespace verbs
