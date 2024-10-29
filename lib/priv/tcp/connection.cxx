#include "priv/tcp/connection.hxx"

#include <arpa/inet.h>

#include "priv/tcp/endpoint.hxx"
#include "util/fatal.hxx"

namespace tcp {

namespace {

int setup_and_bind(std::string_view ip, uint16_t port) {
  // create socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < -1) {
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

}  // namespace

Acceptor::Acceptor(std::string local_ip, uint16_t local_port) : sock(setup_and_bind(local_ip, local_port)) {}

Acceptor::~Acceptor() {
  if (sock != -1) {
    if (auto ec = ::close(sock); ec < 0) {
      die("Fail to close listening socket {}, errno: {}", sock, errno);
    }
  }
};

void Acceptor::listen_and_accept() {
  if (auto ec = ::listen(sock, 10); ec < 0) {
    die("Fail to listen, errno: {}", errno);
  }
  for (auto &endpoint : pending_endpoints) {
    sockaddr_in client_addr_in = {};
    socklen_t client_addr_len = sizeof(sockaddr);
    auto client_sock = ::accept(sock, reinterpret_cast<sockaddr *>(&client_addr_in), &client_addr_len);
    if (client_sock < 0) {
      die("Fail to accept connection, errno: {}", errno);
    }
    TRACE(get_socket_connection_info(client_sock));
    endpoint.get().sock = client_sock;
  }
}

Acceptor &Acceptor::associate(EndpointRefs<Side::ServerSide> &&es) {
  pending_endpoints.insert(pending_endpoints.end(), std::make_move_iterator(es.begin()),
                           std::make_move_iterator(es.end()));
  return *this;
}

Connector ::Connector(std::string remote_ip, uint16_t remote_port)
    : remote_addr_in({
          .sin_family = AF_INET,
          .sin_port = htons(remote_port),
          .sin_addr = {.s_addr = inet_addr(remote_ip.data())},
          .sin_zero = {},
      }) {}

void Connector::connect(Endpoint<Side::ClientSide> &e, std::string local_ip, uint16_t local_port) {
  assert(e.ready());
  auto sock = setup_and_bind(local_ip, local_port);
  if (auto ec = ::connect(sock, reinterpret_cast<sockaddr *>(&remote_addr_in), sizeof(remote_addr_in)); ec < 0) {
    die("Fail to connect with remote server {}, errno: {}", inet_ntoa(remote_addr_in.sin_addr),
        ntohs(remote_addr_in.sin_port), errno);
  }
  TRACE(get_socket_connection_info(sock));
  e.sock = sock;
}

}  // namespace tcp
