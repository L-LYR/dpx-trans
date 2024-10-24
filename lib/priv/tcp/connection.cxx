#include "priv/tcp/connection.hxx"

#include <arpa/inet.h>

#include "priv/tcp/endpoint.hxx"
#include "util/fatal.hxx"

namespace tcp {

Connection::~Connection() {
  if (sock != -1) {
    if (auto ec = ::close(sock); ec < 0) {
      die("Fail to close socket {}, connection {}:{}-{}:{}, errno: {}", sock, local_ip, local_port, remote_ip,
          remote_port, errno);
    }
  }
}

Connection::Connection(Side side_, int sock_) : ConnectionBase(side_), sock(sock_) {
  sockaddr_in addr_in = {};
  socklen_t addr_in_len = sizeof(addr_in);
  if (auto ec = ::getsockname(sock, reinterpret_cast<sockaddr *>(&addr_in), &addr_in_len); ec < 0) {
    die("Fail to get local addr, errno: {}", errno);
  }
  local_ip = inet_ntoa(addr_in.sin_addr);
  local_port = ntohs(addr_in.sin_port);
  if (auto ec = ::getpeername(sock, reinterpret_cast<sockaddr *>(&addr_in), &addr_in_len); ec < 0) {
    die("Fail to get remote addr, errno: {}", errno);
  }
  remote_ip = inet_ntoa(addr_in.sin_addr);
  local_port = ntohs(addr_in.sin_port);
}

void Connection::establish(Side side, int sock, Endpoint &e) { e.conn = ConnectionPtr(new Connection(side, sock)); }

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

}  // namespace

Acceptor::Acceptor(std::string local_ip, uint16_t local_port)
    : ConnectionHandleBase(Side::ServerSide), sock(setup_and_bind(local_ip, local_port)) {}

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
  for (auto &endpoint : endpoints) {
    sockaddr_in client_addr_in = {};
    socklen_t client_addr_len = sizeof(sockaddr);
    auto client_sock = ::accept(sock, reinterpret_cast<sockaddr *>(&client_addr_in), &client_addr_len);
    if (client_sock < 0) {
      die("Fail to accept connection, errno: {}", errno);
    }
    Connection::establish(side, client_sock, endpoint);
  }
}

Acceptor &Acceptor::associate(EndpointRefs &&endpoints_) {
  endpoints = endpoints_;
  return *this;
}

Connector ::Connector(std::string remote_ip, uint16_t remote_port)
    : ConnectionHandleBase(Side::ClientSide),
      remote_addr_in({
          .sin_family = AF_INET,
          .sin_port = htons(remote_port),
          .sin_addr = {.s_addr = inet_addr(remote_ip.data())},
          .sin_zero = {},
      }) {}

void Connector::connect(Endpoint &e, std::string local_ip, uint16_t local_port) {
  auto sock = setup_and_bind(local_ip, local_port);
  if (auto ec = ::connect(sock, reinterpret_cast<sockaddr *>(&remote_addr_in), sizeof(remote_addr_in)); ec < 0) {
    die("Fail to connect with remote server {}, errno: {}", inet_ntoa(remote_addr_in.sin_addr),
        ntohs(remote_addr_in.sin_port), errno);
  }
  Connection::establish(side, sock, e);
}

}  // namespace tcp
