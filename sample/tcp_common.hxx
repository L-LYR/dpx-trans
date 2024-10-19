#pragma once

#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "memory/simple_buffer.hxx"
#include "util/fatal.hxx"
#include "util/hex_dump.hxx"
#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

enum class Side {
  ClientSide,
  ServerSide,
};

using Buffers = std::vector<Buffer>;

class Acceptor;
class Connector;
// created from a Connector
class Endpoint : Noncopyable, Nonmovable {
  friend class Acceptor;
  friend class Connector;

 public:
  Endpoint(/* Context ctx, */ Buffers &&buffers_) : buffers(std::move(buffers_)) {
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
    if (auto ec = close(sock); ec < 0) {
      die("Fail to close socket, errno: {}", errno);
    }
  };

  bool is_established() { return sock != -1; }

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
      SPDLOG_CRITICAL("Mismatched Fn?");
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
        SPDLOG_ERROR("Something wrong with one cqe, but ignored");
      }
    });
  }

  template <typename Fn>
  void wait_and_then(Fn &&fn) {
    io_uring_cqe *cqe = nullptr;
    if (auto ec = io_uring_wait_cqe(&ring, &cqe); ec < 0) {
      die("Fail to wait cqe, errno {}", -ec);
    }
    if constexpr (std::is_invocable_v<Fn, int>) {
      fn(cqe->res);
    } else if constexpr (std::is_invocable_v<Fn, int, uint64_t>) {
      fn(cqe->res, io_uring_cqe_get_data64(cqe));
    } else if constexpr (std::is_invocable_v<Fn, int, void *>) {
      fn(cqe->res, io_uring_cqe_get_data(cqe));
    } else {
      // do nothing
      SPDLOG_CRITICAL("Mismatched Fn?");
    }
    io_uring_cqe_seen(&ring, cqe);
  }

  void post_write(Buffer &in, size_t nbytes) {
    assert(sock != -1);
    assert(nbytes <= in.size());
    auto w_sqe = io_uring_get_sqe(&ring);
    io_uring_prep_write_fixed(w_sqe, sock, in.data(), nbytes, 0, in.index());
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit write sqe, errno {}", -ec);
    }
  }

  void post_read(Buffer &out, size_t nbytes) {
    assert(sock != -1);
    assert(nbytes);
    auto r_sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read_fixed(r_sqe, sock, out.data(), nbytes, 0, out.index());
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit read sqe, errno {}", -ec);
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
        die("Fail to write payload, errno {}", -n);
      }
      SPDLOG_INFO("Write {} bytes", n);
    });
  }

  template <typename Payload>
  Payload read() {
    auto &out = get_buffer();
    post_read(out, out.size());
    auto resp = Payload{};
    wait_and_then([&resp, &out](int n) {
      if (n < 0) {
        die("Fail to read payload, errno {}", -n);
      }
      SPDLOG_INFO("Read {} bytes", n);
      std::cout << std::endl << Hexdump(out.data(), n) << std::endl;
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
    idx = (idx + 1) % buffers.size();
    buffers[idx].clear();
    return buffers[idx];
  }
  int ring_fd() { return ring.ring_fd; }
  void set_sock(int sock_) { sock = sock_; }

  io_uring ring;
  int sock = -1;
  Buffers buffers;
  uint32_t idx = 0;
};

class ConnectionHandle : Noncopyable {
 public:
  ConnectionHandle(Side side_) : side(side_) {}
  ~ConnectionHandle() { close(); };

  ConnectionHandle(ConnectionHandle &&other) {
    side = other.side;
    sock = std::exchange(other.sock, -1);
  }
  ConnectionHandle &operator=(ConnectionHandle &&other) {
    if (this != &other) {
      close();
      side = other.side;
      sock = std::exchange(other.sock, -1);
    }
    return *this;
  }

 protected:
  void close() {
    if (sock != -1) {
      if (auto ec = ::close(sock); ec < 0) {
        die("Fail to close socket, errno: {}", errno);
      }
    }
  }

  void setup(std::string_view ipv4, uint16_t port) {
    // create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < -1) {
      die("Fail to create server side socket, errno {}", errno);
    }
    // set socket option, reusable
    bool enable = true;
    if (auto ec = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &enable, sizeof(int)); ec < 0) {
      die("Fail to set socket options, errno {}", errno);
    }
    // bind
    auto ip_in = inet_addr(ipv4.data());
    if (ip_in == INADDR_NONE) {
      die("Wrong format local ip {}", ipv4);
    }
    auto add_in = sockaddr_in{
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr = {.s_addr = ip_in},
        .sin_zero = {},
    };
    if (auto ec = bind(sock, reinterpret_cast<const sockaddr *>(&add_in), sizeof(add_in)); ec < 0) {
      die("Fail to bind {}:{}, errno {}", ipv4, port, errno);
    }
  }

  Side side;
  int sock = -1;
};

class Acceptor : ConnectionHandle {
  using EndpointRef = std::reference_wrapper<Endpoint>;
  using EndpointRefs = std::vector<EndpointRef>;

 public:
  Acceptor(EndpointRefs &&endpoints_, std::string ip_, uint16_t port_)
      : ConnectionHandle(Side::ServerSide), endpoints(std::move(endpoints_)), ip(ip_), port(port_) {
    // initialize ring
    if (auto ec = io_uring_queue_init(endpoints.size() * 2, &ring, 0); ec < 0) {
      die("Fail to init ring, errno {}", -ec);
    }
    setup(ip, port);
  }
  ~Acceptor() { io_uring_queue_exit(&ring); };

  void listen() {
    // listen
    if (auto ec = ::listen(sock, /* backlog */ 10); ec < 0) {
      die("Fail to listen {}:{}, errno {}", ip, port, errno);
    }
    std::vector<accept_ctx_t> ctxs(endpoints.size());
    for (auto i = 0uz; i < endpoints.size(); ++i) {
      auto accept_sqe = io_uring_get_sqe(&ring);
      io_uring_prep_accept(accept_sqe, sock, reinterpret_cast<sockaddr *>(&ctxs[i].addr), &ctxs[i].addr_len, 0);
      ctxs[i].index = i;
      io_uring_sqe_set_data(accept_sqe, &ctxs[i]);
    }
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit accept sqes, errno {}", -ec);
    }
    io_uring_cqe *cqe = nullptr;
    auto established = 0uz;
    auto accepted = 0uz;
    while (established < endpoints.size() && accepted < endpoints.size()) {
      if (auto ec = io_uring_wait_cqe(&ring, &cqe); ec < 0) {
        die("Fail to wait cqe, errno {}", -ec);
      }
      if (cqe->res < 0) {
        die("Fail to accept one connection, errno {}", -cqe->res);
      }
      if (auto data = io_uring_cqe_get_data(cqe); data == nullptr) {
        SPDLOG_INFO("establish one connection");
        if (cqe->res != 0) {
          die("Fail to send msg, errno {}", -cqe->res);
        }
        ++established;
      } else {
        auto ctx = reinterpret_cast<accept_ctx_t *>(data);
        SPDLOG_INFO("accept client addr: {}:{}", inet_ntoa(ctx->addr.sin_addr), ntohs(ctx->addr.sin_port));
        auto msg_sqe = io_uring_get_sqe(&ring);
        auto &endpoint = endpoints[ctx->index].get();
        endpoint.set_sock(cqe->res);
        io_uring_prep_msg_ring(msg_sqe, endpoint.ring_fd(), 0, 0, 0);
        io_uring_sqe_set_data(msg_sqe, nullptr);
        if (auto ec = io_uring_submit(&ring); ec < 0) {
          die("Fail to submit establish sqe, errno {}", -ec);
        }
        ++accepted;
      }
      io_uring_cqe_seen(&ring, cqe);
    }
  }

 private:
  struct accept_ctx_t {
    sockaddr_in addr = {};
    socklen_t addr_len = sizeof(addr);
    size_t index = -1;
  };

  EndpointRefs endpoints;
  io_uring ring;
  std::string ip = "";
  uint16_t port = -1;
};

class Connector : ConnectionHandle {
 public:
  Connector(Endpoint &endpoint_, std::string remote_ip_, uint16_t remote_port_, std::string local_ip_,
            uint16_t local_port_)
      : ConnectionHandle(Side::ClientSide),
        endpoint(endpoint_),
        remote_ip(remote_ip_),
        local_ip(local_ip_),
        remote_port(remote_port_),
        local_port(local_port_) {
    setup(local_ip, local_port);
  }

  ~Connector() {}

  void connect() {
    // connect
    auto remote_ip_in = inet_addr(remote_ip.c_str());
    if (remote_ip_in == INADDR_NONE) {
      die("Wrong format remote ip {}", remote_ip);
    }
    auto srv_addr = sockaddr_in{
        .sin_family = AF_INET,
        .sin_port = htons(remote_port),
        .sin_addr = {.s_addr = remote_ip_in},
        .sin_zero = {},
    };
    if (auto ec = ::connect(sock, reinterpret_cast<sockaddr *>(&srv_addr), sizeof(srv_addr)); ec < 0) {
      die("Fail to connect with remote server {}:{}, errno {}", remote_ip, remote_port, errno);
    }
    std::cout << "Ok!" << std::endl;
    endpoint.set_sock(std::exchange(sock, -1));
  }

 private:
  Endpoint &endpoint;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = -1;
  uint16_t local_port = -1;
};

struct PayloadType {
  uint32_t id;
  std::string message;
};

struct EchoRpc {
  using Request = PayloadType;
  using Response = PayloadType;
};