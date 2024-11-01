#pragma once

#include <rdma/rdma_cma.h>

#include "priv/common.hxx"
#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

namespace verbs {

class Endpoint;

class EventChannel : Noncopyable, Nonmovable {
 public:
  explicit EventChannel(rdma_event_channel* p_);
  EventChannel();
  ~EventChannel();
  rdma_cm_event* wait(rdma_cm_event_type expected);
  void ack(rdma_cm_event* e);
  void wait_and_ack(rdma_cm_event_type expected);

  bool own = false;
  rdma_event_channel* p = nullptr;
};

class Acceptor : public ConnectionHandleBase<Acceptor, Endpoint> {
 public:
  Acceptor(std::string local_ip, uint16_t local_port);
  ~Acceptor();

 public:
  void listen_and_accept();
  void wait_for_disconnect();

 private:
  EventChannel c;
  rdma_cm_id* id = nullptr;
  EndpointRefs pending_endpoints;
};

class Connector : public ConnectionHandleBase<Connector, Endpoint> {
 public:
  Connector(std::string remote_ip, uint16_t remote_port);
  ~Connector() = default;

  void connect(Endpoint& endpoint, std::string local_ip, uint16_t local_port = 0);

 private:
  sockaddr_in remote_addr_in = {};
};

}  // namespace verbs
