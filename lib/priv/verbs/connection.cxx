#include "priv/verbs/connection.hxx"

#include <arpa/inet.h>

#include <cassert>

#include "priv/verbs/endpoint.hxx"
#include "util/fatal.hxx"

namespace verbs {

EventChannel::EventChannel(rdma_event_channel* p_) : own(false), p(p_) {}

EventChannel::EventChannel() : own(true) {
  if (p = rdma_create_event_channel(); p == nullptr) {
    die("Fail to create event channel, errno: {}", errno);
  }
}

EventChannel::~EventChannel() {
  if (own && p != nullptr) {
    rdma_destroy_event_channel(p);
  }
}

[[nodiscard("Must not ignore the cm event")]] rdma_cm_event* EventChannel::wait(rdma_cm_event_type expected) {
  rdma_cm_event* e = nullptr;
  if (auto ec = rdma_get_cm_event(p, &e); ec < 0) {
    die("Fail to get expected event {}, errno: {}", rdma_event_str(expected), errno);
  } else if (e->status != 0) {
    die("Get a bad event {}, status: {}, expect {}", rdma_event_str(e->event), e->status, rdma_event_str(expected));
  } else if (e->event != expected) {
    die("Expect event {}, but get event {}", rdma_event_str(expected), rdma_event_str(e->event));
  }
  return e;
}

void EventChannel::ack(rdma_cm_event* e) {
  assert(e != nullptr);
  if (auto ec = rdma_ack_cm_event(e); ec < 0) {
    die("Fail to ack cm event {}, errno: {}", rdma_event_str(e->event), errno);
  }
}

void EventChannel::wait_and_ack(rdma_cm_event_type expected) { ack(wait(expected)); }

namespace {

rdma_cm_id* setup_and_bind(Side side, EventChannel& c, std::string_view ip, uint16_t port) {
  rdma_cm_id* id = nullptr;
  if (auto ec = rdma_create_id(c.p, &id, nullptr, RDMA_PS_TCP); ec < 0) {
    die("Fail to create cm id, errno: {}", errno);
  }
  rdma_addrinfo hints = {};
  hints.ai_flags = RAI_NUMERICHOST | RAI_FAMILY | (side == Side::ServerSide ? RAI_PASSIVE : 0);
  hints.ai_family = AF_INET;
  auto port_str = std::to_string(port);
  rdma_addrinfo* addr = nullptr;
  if (auto ec = rdma_getaddrinfo(ip.data(), port_str.data(), &hints, &addr); ec < 0) {
    die("Fail to get addrinfo, errno: {}", errno);
  }
  if (auto ec = rdma_bind_addr(id, (side == Side::ServerSide ? addr->ai_src_addr : addr->ai_dst_addr)); ec < 0) {
    die("Fail to bind {}:{}, errno: {}", ip, port, errno);
  }
  rdma_freeaddrinfo(addr);
  return id;
}

std::string get_cm_connection_info(rdma_cm_id* id) {
  auto local_addr_in = reinterpret_cast<sockaddr_in*>(rdma_get_local_addr(id));
  auto local_addr = std::format("{}:{}", inet_ntoa(local_addr_in->sin_addr), ntohs(local_addr_in->sin_port));
  auto remote_addr_in = reinterpret_cast<sockaddr_in*>(rdma_get_peer_addr(id));
  auto remote_addr = std::format("{}:{}", inet_ntoa(remote_addr_in->sin_addr), ntohs(remote_addr_in->sin_port));
  return std::format("Connection {} <-> {}", local_addr, remote_addr);
}

}  // namespace

Acceptor::Acceptor(std::string local_ip, uint16_t local_port)
    : c(), id(setup_and_bind(Side::ServerSide, c, local_ip, local_port)) {}

Acceptor::~Acceptor() {
  if (id != nullptr) {
    if (auto ec = rdma_destroy_id(id); ec < 0) {
      die("Fail to destroy listening id, errno: {}", errno);
    }
  }
}

Acceptor& Acceptor::associate(EndpointRefs&& es) {
  pending_endpoints.insert(pending_endpoints.end(), std::make_move_iterator(es.begin()),
                           std::make_move_iterator(es.end()));
  return *this;
}

void Acceptor::listen_and_accept() {
  if (auto ec = rdma_listen(id, 10); ec < 0) {
    die("Fail to listen, errno: {}", errno);
  }
  for (auto& er : pending_endpoints) {
    auto& endpoint = er.get();
    auto e = c.wait(RDMA_CM_EVENT_CONNECT_REQUEST);
    endpoint.id = e->id;
    endpoint.setup_remote_param(e->param.conn);
    c.ack(e);

    if (auto ec = rdma_migrate_id(endpoint.id, endpoint.c.p); ec < 0) {
      die("Fail to migrate cm id to new event channel, errno: {}", errno);
    }
    endpoint.setup_resources();
    if (auto ec = rdma_accept(endpoint.id, &endpoint.local); ec < 0) {
      die("Fail to accept connection, errno: {}", errno);
    }
    endpoint.c.wait_and_ack(RDMA_CM_EVENT_ESTABLISHED);
    TRACE(get_cm_connection_info(endpoint.id));
  }
  pending_endpoints.clear();
}

Connector ::Connector(std::string remote_ip, uint16_t remote_port)
    : remote_addr_in({
          .sin_family = AF_INET,
          .sin_port = htons(remote_port),
          .sin_addr = {.s_addr = inet_addr(remote_ip.data())},
          .sin_zero = {},
      }) {}

void Connector::connect(Endpoint& endpoint, std::string local_ip, uint16_t local_port) {
  auto& c = endpoint.c;
  auto& id = endpoint.id;
  id = setup_and_bind(Side::ClientSide, c, local_ip, local_port);
  if (auto ec = rdma_resolve_addr(endpoint.id, nullptr, reinterpret_cast<sockaddr*>(&remote_addr_in), 10); ec < 0) {
    die("Fail to resolve addr {}, errno: {}", inet_ntoa(remote_addr_in.sin_addr), ntohs(remote_addr_in.sin_port),
        errno);
  }
  c.wait_and_ack(RDMA_CM_EVENT_ADDR_RESOLVED);
  if (auto ec = rdma_resolve_route(id, 10); ec < 0) {
    die("Fail to resolve route, errno: {}", errno);
  }
  c.wait_and_ack(RDMA_CM_EVENT_ROUTE_RESOLVED);
  endpoint.setup_resources();
  if (auto ec = rdma_connect(id, &endpoint.local); ec < 0) {
    die("Fail to establish connection, errno: {}", errno);
  }
  auto e = c.wait(RDMA_CM_EVENT_ESTABLISHED);
  endpoint.setup_remote_param(e->param.conn);
  c.ack(e);
  TRACE(get_cm_connection_info(id));
}

}  // namespace verbs
