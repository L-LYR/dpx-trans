#pragma once

#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "common.hxx"
#include "memory/simple_buffer.hxx"
#include "util/fatal.hxx"
#include "util/hex_dump.hxx"
#include "util/logger.hxx"
#include "util/noncopyable.hxx"

using Buffers = std::vector<Buffer>;

class Connection;
using ConnectionPtr = std::unique_ptr<Connection>;

class Connection : public ConnectionBase {
  friend class Acceptor;
  friend class Connector;
  friend class Endpoint;

 public:
  ~Connection() {
    if (sock != -1) {
      if (auto ec = ::close(sock); ec < 0) {
        die("Fail to close socket {}, connection {}:{}-{}:{}, errno: {}", sock, local_ip, local_port, remote_ip,
            remote_port, errno);
      }
    }
  }

 private:
  static ConnectionPtr create(Side side, int sock) { return ConnectionPtr(new Connection(side, sock)); }

  void establish() {
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

  Connection(Side side_, int sock_) : ConnectionBase(side_), sock(sock_) {}

  int sock = -1;
};

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;

 public:
  Endpoint(Buffers &&buffers_) : buffers(std::move(buffers_)) {
    if (auto ec = io_uring_queue_init(16, &ring, 0); ec < 0) {
      die("Fail to init ring, errno: {}", -ec);
    }
    auto iovecs = std::vector<iovec>(buffers.size());
    for (auto i = 0uz; i < buffers.size(); ++i) {
      iovecs[i].iov_base = buffers[i].data();
      iovecs[i].iov_len = buffers[i].size();
      buffers[i].set_index(i);
    }
    if (auto ec = io_uring_register_buffers(&ring, iovecs.data(), buffers.size()); ec < 0) {
      die("Fail to register buffers, errno: {}", -ec);
    }
  }
  ~Endpoint() {
    if (auto ec = io_uring_unregister_buffers(&ring); ec < 0) {
      die("Fail to unregister buffers, errno: {}", -ec);
    }
    io_uring_queue_exit(&ring);
  };

  template <typename Fn>
  bool try_wait_and_then(Fn &&fn) {
    io_uring_cqe *cqe = nullptr;
    if (io_uring_peek_cqe(&ring, &cqe) == 0) {
      return false;
    }
    if constexpr (std::is_invocable_v<Fn, int>) {
      fn(cqe->res);
    } else if constexpr (std::is_invocable_v<Fn, int, uint64_t>) {
      fn(cqe->res, io_uring_cqe_get_data64(cqe));
    } else if constexpr (std::is_invocable_v<Fn, int, void *>) {
      fn(cqe->res, io_uring_cqe_get_data(cqe));
    } else {
      // do nothing
      CRITICAL("Mismatched Fn?");
    }
    io_uring_cqe_seen(&ring, cqe);
    return true;
  }

  void try_wait_nr(int /*n*/) {
    // todo
  }

  void wait_nr(int /*n*/) {
    // todo
  }

  void wait_and_ignore() {
    wait_and_then([](int res) {
      if (res != 0) {
        ERROR("Something wrong with one cqe, but ignored");
      }
    });
  }

  template <typename Fn>
  void wait_and_then(Fn &&fn) {
    io_uring_cqe *cqe = nullptr;
    if (auto ec = io_uring_wait_cqe(&ring, &cqe); ec < 0) {
      die("Fail to wait cqe, errno: {}", -ec);
    }
    if constexpr (std::is_invocable_v<Fn, int>) {
      fn(cqe->res);
    } else if constexpr (std::is_invocable_v<Fn, int, uint64_t>) {
      fn(cqe->res, io_uring_cqe_get_data64(cqe));
    } else if constexpr (std::is_invocable_v<Fn, int, void *>) {
      fn(cqe->res, io_uring_cqe_get_data(cqe));
    } else {
      // do nothing
      CRITICAL("Mismatched Fn?");
    }
    io_uring_cqe_seen(&ring, cqe);
  }

  void post_write(Buffer &in, size_t nbytes) {
    assert(nbytes <= in.size());
    auto w_sqe = io_uring_get_sqe(&ring);
    io_uring_prep_write_fixed(w_sqe, conn->sock, in.data(), nbytes, 0, in.index());
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit write sqe, errno: {}", -ec);
    }
  }

  void post_read(Buffer &out, size_t nbytes) {
    assert(nbytes);
    auto r_sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read_fixed(r_sqe, conn->sock, out.data(), nbytes, 0, out.index());
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit read sqe, errno: {}", -ec);
    }
  }

  template <typename Payload>
  void write(Payload &&payload) {
    auto &in = get_buffer();
    auto serializer = zpp::bits::out(in);
    serializer(payload).or_throw();
    std::cout << std::endl << Hexdump(in.data(), serializer.position()) << std::endl;
    post_write(in, serializer.position());
    wait_and_then([](int n) {
      if (n < 0) {
        die("Fail to write payload, errno: {}", -n);
      }
      INFO("Write {} bytes", n);
    });
  }

  template <typename Payload>
  Payload read() {
    auto &out = get_buffer();
    post_read(out, out.size());
    auto resp = Payload{};
    wait_and_then([&resp, &out](int n) {
      if (n < 0) {
        die("Fail to read payload, errno: {}", -n);
      }
      std::cout << std::endl << Hexdump(out.data(), n) << std::endl;
      INFO("Read {} bytes", n);
      auto deserializer = zpp::bits::in(out);
      deserializer(resp).or_throw();
    });
    return resp;
  }

  template <typename Rpc>
  using req_t = typename Rpc::Request;

  template <typename Rpc>
  using resp_t = typename Rpc::Response;

  template <typename Rpc>
  resp_t<Rpc> call(req_t<Rpc> &&r) {
    write(std::forward<req_t<Rpc>>(r));
    return read<resp_t<Rpc>>();
  }

 private:
  // TODO better one
  Buffer &get_buffer() {
    active_buffer_idx = (active_buffer_idx + 1) % buffers.size();
    buffers[active_buffer_idx].clear();
    return buffers[active_buffer_idx];
  }

  void establish(ConnectionPtr conn_) {
    conn = std::move(conn_);
    conn->establish();
  }

  ConnectionPtr conn = nullptr;
  io_uring ring;
  Buffers buffers;
  uint32_t active_buffer_idx = 0;
};

using EndpointRef = std::reference_wrapper<Endpoint>;
using EndpointRefs = std::vector<EndpointRef>;

class ConnectionHandle : Noncopyable {
 public:
  ConnectionHandle(Side side_) : side(side_) {}
  ~ConnectionHandle() {}

 protected:
  inline static int setup_and_bind(std::string_view ip, uint16_t port) {
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
      die("Wrong format local ip {}", ip);
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

  Side side;
};

class Acceptor : public ConnectionHandle {
 public:
  Acceptor(std::string local_ip, uint16_t local_port)
      : ConnectionHandle(Side::ServerSide), sock(setup_and_bind(local_ip, local_port)) {}
  ~Acceptor() {
    if (sock != -1) {
      if (auto ec = ::close(sock); ec < 0) {
        die("Fail to close listening socket {}, errno: {}", sock, errno);
      }
    }
  };

  Acceptor &associate(EndpointRefs &&endpoints_) {
    endpoints = endpoints_;
    return *this;
  }

  void listen_and_accept() {
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
      endpoint.get().establish(Connection::create(side, client_sock));
    }
  }

 private:
  EndpointRefs endpoints;
  int sock = -1;  // listening sock
};

class Connector : public ConnectionHandle {
 public:
  Connector(std::string remote_ip, uint16_t remote_port)
      : ConnectionHandle(Side::ClientSide),
        remote_addr_in({
            .sin_family = AF_INET,
            .sin_port = htons(remote_port),
            .sin_addr = {.s_addr = inet_addr(remote_ip.data())},
            .sin_zero = {},
        }) {}

  ~Connector() = default;

  void connect(Endpoint &e, std::string local_ip = "", uint16_t local_port = 0 /* 0 for INPORT_ANY */) {
    auto sock = setup_and_bind(local_ip, local_port);
    if (auto ec = ::connect(sock, reinterpret_cast<sockaddr *>(&remote_addr_in), sizeof(remote_addr_in)); ec < 0) {
      die("Fail to connect with remote server {}, errno: {}", inet_ntoa(remote_addr_in.sin_addr),
          ntohs(remote_addr_in.sin_port), errno);
    }
    e.establish(Connection::create(side, sock));
  }

 private:
  sockaddr_in remote_addr_in = {};
};

struct PayloadType {
  uint32_t id;
  std::string message;
};

struct EchoRpc {
  using Request = PayloadType;
  using Response = PayloadType;
};