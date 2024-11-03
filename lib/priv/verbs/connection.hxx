#pragma once

#include <rdma/rdma_cma.h>

#include <string>
#include <vector>

#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

namespace verbs {

struct ConnectionParam {
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = 0;
  uint16_t local_port = 0;
};

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

class ConnectionHandle {
  using EndpointRef = std::reference_wrapper<Endpoint>;
  using EndpointRefs = std::vector<EndpointRef>;

 public:
  ConnectionHandle(const ConnectionParam& param_);

  ~ConnectionHandle();

  ConnectionHandle& associate(Endpoint& e);

  ConnectionHandle& associate(EndpointRefs&& es);

  void listen_and_accept();
  void wait_for_disconnect();

  void connect();
  void disconnect();

 private:
  EventChannel c;
  rdma_cm_id* id = nullptr;
  const ConnectionParam& param;
  EndpointRefs pending_endpoints;
};

}  // namespace verbs
