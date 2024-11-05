#include "priv/verbs/connection.hxx"

#include <arpa/inet.h>

#include <cassert>

#include "priv/verbs/endpoint.hxx"
#include "util/fatal.hxx"

namespace verbs {

EventChannel::EventChannel(rdma_event_channel* p_) : p(p_) {}

EventChannel::EventChannel() {
  if (p = rdma_create_event_channel(); p == nullptr) {
    die("Fail to create event channel, errno: {}", errno);
  }
}

EventChannel::~EventChannel() {
  if (p != nullptr) {
    rdma_destroy_event_channel(p);
  }
}

[[nodiscard("Must not ignore the cm event")]] rdma_cm_event* EventChannel::get_event() {
  rdma_cm_event* e = nullptr;
  if (auto ec = rdma_get_cm_event(p, &e); ec < 0) {
    die("Fail to get event, errno: {}", errno);
  }
  return e;
}

[[nodiscard("Must not ignore the cm event")]] rdma_cm_event* EventChannel::wait(rdma_cm_event_type expected) {
  auto e = get_event();
  if (e->status != 0) {
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

ConnectionHandle::ConnectionHandle(const ConnectionParam& param_) : param(param_) {}

ConnectionHandle::~ConnectionHandle() {
  if (id != nullptr) {
    if (auto ec = rdma_destroy_id(id); ec < 0) {
      die("Fail to destroy listening id, errno: {}", errno);
    }
  }
}

ConnectionHandle& ConnectionHandle::associate(Endpoint& e) {
  pending_endpoints.emplace_back(e);
  return *this;
}

ConnectionHandle& ConnectionHandle::associate(EndpointRefs&& es) {
  pending_endpoints.insert(pending_endpoints.end(), std::make_move_iterator(es.begin()),
                           std::make_move_iterator(es.end()));
  return *this;
}

void ConnectionHandle::listen_and_accept() {
  id = setup_and_bind(Side::ServerSide, c, param.local_ip, param.local_port);
  if (auto ec = rdma_listen(id, pending_endpoints.size()); ec < 0) {
    die("Fail to listen, errno: {}", errno);
  }
  std::ranges::for_each(pending_endpoints, [this](Endpoint& e) {
    auto event = c.wait(RDMA_CM_EVENT_CONNECT_REQUEST);
    e.id = event->id;
    e.setup_remote_param(event->param.conn);
    e.prepare();
    if (auto ec = rdma_accept(e.id, &e.local); ec < 0) {
      die("Fail to accept connection, errno: {}", errno);
    }
    e.id->context = &e;
    c.ack(event);
    c.wait_and_ack(RDMA_CM_EVENT_ESTABLISHED);
    DEBUG(get_cm_connection_info(e.id));
    e.run();
  });
}

void ConnectionHandle::wait_for_disconnect() {
  for (auto n_disconnected = 0uz; n_disconnected < pending_endpoints.size(); ++n_disconnected) {
    auto e = c.wait(RDMA_CM_EVENT_DISCONNECTED);
    auto endpoint = reinterpret_cast<Endpoint*>(e->id->context);
    c.ack(e);

    endpoint->stop();
    if (auto ec = rdma_disconnect(endpoint->id); ec < 0) {
      die("Fail to disconnect, errno: {}", errno);
    }
    endpoint->shutdown();
  }
}

void ConnectionHandle::connect() {
  auto remote_addr_in = sockaddr_in{
      .sin_family = AF_INET,
      .sin_port = htons(param.remote_port),
      .sin_addr = {.s_addr = inet_addr(param.remote_ip.data())},
      .sin_zero = {},
  };
  std::ranges::for_each(pending_endpoints, [this, i = 0, &remote_addr_in](Endpoint& e) mutable {
    e.id = setup_and_bind(Side::ClientSide, c, param.local_ip, (param.local_port != 0 ? param.local_port + i : 0));
    if (auto ec = rdma_resolve_addr(e.id, nullptr, reinterpret_cast<sockaddr*>(&remote_addr_in), 10); ec < 0) {
      die("Fail to resolve addr {}:{}, errno: {}", param.remote_ip, param.remote_port, errno);
    }
    c.wait_and_ack(RDMA_CM_EVENT_ADDR_RESOLVED);
    if (auto ec = rdma_resolve_route(e.id, 10); ec < 0) {
      die("Fail to resolve route, errno: {}", errno);
    }
    c.wait_and_ack(RDMA_CM_EVENT_ROUTE_RESOLVED);
    e.prepare();
    if (auto ec = rdma_connect(e.id, &e.local); ec < 0) {
      die("Fail to establish connection, errno: {}", errno);
    }
    auto event = c.wait(RDMA_CM_EVENT_ESTABLISHED);
    e.setup_remote_param(event->param.conn);
    c.ack(event);
    DEBUG(get_cm_connection_info(e.id));
    e.run();
    i++;
  });
}

void ConnectionHandle::disconnect() {
  std::ranges::for_each(pending_endpoints, [](Endpoint& e) {
    e.stop();
    if (auto ec = rdma_disconnect(e.id); ec < 0) {
      die("Fail to disconnect, errno: {}", errno);
    }
    e.shutdown();
  });
}

}  // namespace verbs
