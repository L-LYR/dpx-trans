#include "priv/tcp/connection.hxx"

#include <arpa/inet.h>

#include "priv/tcp/endpoint.hxx"
#include "util/fatal.hxx"

namespace tcp {

namespace {

int setup_and_bind(std::string_view ip, uint16_t port) {
  // create socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    die("Fail to create server side socket, errno: {}", errno);
  }
  // set socket option, reusable
  bool enable = true;
  if (auto ec = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &enable, sizeof(int)); ec < 0) {
    die("Fail to set socket options, errno: {}", errno);
  }
  // bind
  auto ip_in = (ip.empty() ? INADDR_ANY : inet_addr(ip.data()));
  if (ip_in == INADDR_NONE) {
    die("Wrong format: {}", ip);
  }
  auto addr_in = sockaddr_in{
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = {.s_addr = ip_in},
      .sin_zero = {},
  };
  if (auto ec = bind(sock, reinterpret_cast<const sockaddr *>(&addr_in), sizeof(addr_in)); ec < 0) {
    die("Fail to bind {}:{}, errno: {}", ip, port, errno);
  }
  return sock;
}

std::string get_socket_connection_info(int sock) {
  sockaddr_in addr_in = {};
  socklen_t addr_in_len = sizeof(addr_in);
  if (auto ec = ::getsockname(sock, reinterpret_cast<sockaddr *>(&addr_in), &addr_in_len); ec < 0) {
    die("Fail to get local addr, errno: {}", errno);
  }
  auto local_addr = std::format("{}:{}", inet_ntoa(addr_in.sin_addr), ntohs(addr_in.sin_port));
  if (auto ec = ::getpeername(sock, reinterpret_cast<sockaddr *>(&addr_in), &addr_in_len); ec < 0) {
    die("Fail to get remote addr, errno: {}", errno);
  }
  auto remote_addr = std::format("{}:{}", inet_ntoa(addr_in.sin_addr), ntohs(addr_in.sin_port));
  return std::format("Connection {} <-> {}", local_addr, remote_addr);
}

void close_socket(int s) {
  if (s > 0) {
    if (auto ec = ::close(s); ec < 0) {
      die("Fail to close socket {}, errno: {}", s, errno);
    }
  }
}

}  // namespace

ConnectionHandle::ConnectionHandle(const ConnectionParam &info_) : ConnectionHandleBase(info_) {}

ConnectionHandle::~ConnectionHandle() { close_socket(conn_sock); };

void ConnectionHandle::listen_and_accept() {
  assert(param.passive);
  auto listen_sock = setup_and_bind(param.local_ip, param.local_port);
  if (auto ec = ::listen(listen_sock, pending_endpoints.size() + 1); ec < 0) {
    die("Fail to listen, errno: {}", errno);
  }
  conn_sock = ::accept(listen_sock, nullptr, nullptr);
  if (conn_sock < 0) {
    die("Fail to accept connection manage socket, errno: {}", errno);
  }
  std::ranges::for_each(pending_endpoints, [listen_sock](Endpoint &e) {
    e.sock = ::accept(listen_sock, nullptr, nullptr);
    if (e.sock < 0) {
      die("Fail to accept connection from peer, errno: {}", errno);
    }
    INFO(get_socket_connection_info(e.sock));
    e.run();
  });
  close_socket(listen_sock);
}

void ConnectionHandle::wait_for_disconnect() {
  assert(param.passive);
  char c = 0;
  TRACE("Wait for disconnection event");
  read(conn_sock, &c, 1);
  // we don't care the return value, any case will indicate the connection is going to close.
  TRACE("Closing");
  std::ranges::for_each(pending_endpoints, [](Endpoint &e) {
    close_socket(e.sock);
    e.stop();
  });
  TRACE("Shutdown");
}

void ConnectionHandle::connect() {
  assert(!param.passive);
  auto remote_addr_in = sockaddr_in{
      .sin_family = AF_INET,
      .sin_port = htons(param.remote_port),
      .sin_addr = {.s_addr = inet_addr(param.remote_ip.data())},
      .sin_zero = {},
  };
  conn_sock = setup_and_bind(param.local_ip, (param.local_port != 0 ? param.local_port + pending_endpoints.size() : 0));
  if (auto ec = ::connect(conn_sock, reinterpret_cast<sockaddr *>(&remote_addr_in), sizeof(remote_addr_in)); ec < 0) {
    die("Fail to connect with peer {}:{}, errno: {}", param.remote_ip, param.remote_port, errno);
  }
  std::ranges::for_each(pending_endpoints, [this, &remote_addr_in, i = 0](Endpoint &e) mutable {
    e.sock = setup_and_bind(param.local_ip, (param.local_port != 0 ? param.local_port + i : 0));
    if (auto ec = ::connect(e.sock, reinterpret_cast<sockaddr *>(&remote_addr_in), sizeof(remote_addr_in)); ec < 0) {
      die("Fail to connect with peer {}:{}, errno: {}", param.remote_ip, param.remote_port, errno);
    }
    INFO(get_socket_connection_info(e.sock));
    e.run();
    i++;
  });
}

void ConnectionHandle::disconnect() {
  assert(!param.passive);
  char c = 'x';
  TRACE("Issue a disconnection event");
  write(conn_sock, &c, 1);
  TRACE("Closing");
  std::ranges::for_each(pending_endpoints, [](Endpoint &e) {
    e.stop();
    close_socket(e.sock);
  });
  TRACE("Shutdown");
}

}  // namespace tcp
