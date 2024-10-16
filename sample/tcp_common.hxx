#pragma once

#include <arpa/inet.h>
#include <dbg.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <zpp_bits.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "util/fatal.hxx"
#include "util/hex_dump.hxx"
#include "util/noncopyable.hxx"

enum class Side {
  ClientSide,
  ServerSide,
};

class Connector : Noncopyable {
 public:
  Connector(std::string remote_ip_, uint16_t remote_port_, std::string local_ip_, uint16_t local_port_)
      : side(Side::ClientSide),
        remote_ip(remote_ip_),
        local_ip(local_ip_),
        remote_port(remote_port_),
        local_port(local_port_) {}
  Connector(std::string local_ip_, uint16_t local_port_)
      : side(Side::ServerSide), local_ip(local_ip_), local_port(local_port_) {}
  ~Connector() {}

  // common
  void do_bind() {
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
    auto local_ip_in = inet_addr(local_ip.c_str());
    if (local_ip_in == INADDR_NONE) {
      die("Wrong format local ip {}", local_ip);
    }
    auto local_addr_in = sockaddr_in{
        .sin_family = AF_INET,
        .sin_port = htons(local_port),
        .sin_addr =
            {
                .s_addr = local_ip_in,
            },
        .sin_zero = {},
    };
    if (auto ec = bind(sock, reinterpret_cast<const sockaddr *>(&local_addr_in), sizeof(local_addr_in)); ec < 0) {
      die("Fail to bind {}:{}, errno {}", local_ip, local_port, errno);
    }
  }

  // server side
  // TODO add a connection establish callback
  void do_listen(std::function<void(int)> conn_cb) {
    // create socket
    assert(side == Side::ServerSide);
    do_bind();
    // listen
    if (auto ec = listen(sock, /* backlog */ 10); ec < 0) {
      die("Fail to listen {}:{}, errno {}", local_ip, local_port, errno);
    }
    // initialize ring
    io_uring ring;
    if (auto ec = io_uring_queue_init(16, &ring, IORING_SETUP_SINGLE_ISSUER); ec < 0) {
      die("Fail to init ring, errno {}", ec);
    }
    // accept connection request
    sockaddr_in clt_addr = {};
    socklen_t clt_addr_len = sizeof(clt_addr);
    io_uring_cqe *cqe = nullptr;
    do {
      // TODO add stop flag
      auto sqe = io_uring_get_sqe(&ring);
      io_uring_prep_accept(sqe, sock, reinterpret_cast<sockaddr *>(&clt_addr), &clt_addr_len, 0);
      io_uring_sqe_set_data(sqe, /* TODO user data */ nullptr);
      io_uring_submit(&ring);
      if (auto ec = io_uring_wait_cqe(&ring, &cqe); ec < 0) {
        die("Fail to wait cqe, errno {}", ec);
      }
      std::cout << std::format("client addr: {}:{}", inet_ntoa(clt_addr.sin_addr), ntohs(clt_addr.sin_port))
                << std::endl;
      // TODO create a server endpoint
      // close(cqe->res);
      conn_cb(cqe->res);
      io_uring_cqe_seen(&ring, cqe);
    } while (true);
    io_uring_queue_exit(&ring);
  }

  // client side
  void do_connect(std::function<void(int)> conn_cb) {
    assert(side == Side::ClientSide);
    do_bind();
    // connect
    auto remote_ip_in = inet_addr(remote_ip.c_str());
    if (remote_ip_in == INADDR_NONE) {
      die("Wrong format remote ip {}", remote_ip);
    }
    auto srv_addr = sockaddr_in{
        .sin_family = AF_INET,
        .sin_port = htons(remote_port),
        .sin_addr =
            {
                .s_addr = remote_ip_in,
            },
        .sin_zero = {},
    };
    if (auto ec = connect(sock, reinterpret_cast<sockaddr *>(&srv_addr), sizeof(srv_addr)); ec < 0) {
      die("Fail to connect with remote server {}:{}, errno {}", remote_ip, remote_port, errno);
    }
    std::cout << "Ok!" << std::endl;
    conn_cb(sock);
  }

 private:
  Side side;
  int sock = -1;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = -1;
  uint16_t local_port = -1;
};

class Buffer : Noncopyable {
 public:
  Buffer(size_t len_ = 1024) : buf(new uint8_t[len_]), len(len_) { clear(); }
  Buffer(Buffer &&other) {
    buf = std::exchange(other.buf, nullptr);
    len = std::exchange(other.len, -1);
    idx = std::exchange(other.idx, -1);
  }
  Buffer &operator=(Buffer &&other) {
    if (this != &other) {
      if (buf != nullptr) {
        delete[] buf;
      }
      buf = std::exchange(other.buf, nullptr);
      len = std::exchange(other.len, -1);
      idx = std::exchange(other.idx, -1);
    }
    return *this;
  }
  ~Buffer() {
    if (buf != nullptr) {
      delete[] buf;
    }
  }

  uint8_t *data() { return buf; }
  const uint8_t *data() const { return buf; }
  size_t size() const { return len; }
  uint8_t &operator[](size_t i) {
    assert(i >= 0 && i < len);
    return buf[i];
  }
  uint8_t operator[](size_t i) const {
    assert(i >= 0 && i < len);
    return buf[i];
  }

  void clear() { memset(buf, 0, len); }

  size_t index() const { return idx; }
  void set_index(size_t idx_) { idx = idx_; }

  operator std::span<uint8_t>() { return std::span<uint8_t>(buf, len); }
  operator std::span<const uint8_t>() const { return std::span<uint8_t>(buf, len); }

 private:
  uint8_t *buf = nullptr;
  size_t len = -1;
  size_t idx = -1;

 public:
  // for zpp_bits inner traits
  using value_type = uint8_t;
};

using Buffers = std::vector<Buffer>;

// created from a Connector
class Endpoint : Noncopyable {
 public:
  Endpoint(/* Context ctx, */ int sock_, Buffers &&buffers_) : sock(sock_), buffers(std::move(buffers_)) {
    io_uring_queue_init(16, &ring, 0);
    auto iovecs = std::vector<iovec>(buffers.size());
    for (auto i = 0uz; i < buffers.size(); i++) {
      iovecs[i].iov_base = buffers[i].data();
      iovecs[i].iov_len = buffers[i].size();
      buffers[i].set_index(i);
    }
    io_uring_register_buffers(&ring, iovecs.data(), buffers.size());
  }
  ~Endpoint() {
    io_uring_queue_exit(&ring);
    close(sock);
  };

  template <typename Payload>
  void post_write(Payload &&payload) {
    auto &in_buffer = get_buffer();
    auto serializer = zpp::bits::out(in_buffer);
    serializer(payload).or_throw();
    std::cout << Hexdump(in_buffer.data(), serializer.position());
    auto w_sqe = io_uring_get_sqe(&ring);
    io_uring_prep_write_fixed(w_sqe, sock, in_buffer.data(), serializer.position(), 0, in_buffer.index());
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit sqe, errno {}", ec);
    }
    io_uring_cqe *cqe = nullptr;
    if (auto ec = io_uring_wait_cqe(&ring, &cqe); ec < 0) {
      die("Fail to wait cqe, errno {}", ec);
    }
    if (cqe->res == -1) {
      die("Fail to write payload");
    }
    std::cout << std::format("write {} bytes", cqe->res) << std::endl;
    io_uring_cqe_seen(&ring, cqe);
  }

  template <typename Payload>
  Payload post_read() {
    auto &out_buffer = get_buffer();
    auto r_sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read_fixed(r_sqe, sock, out_buffer.data(), out_buffer.size(), 0, out_buffer.index());
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit sqe, errno {}", ec);
    }
    io_uring_cqe *cqe = nullptr;
    if (auto ec = io_uring_wait_cqe(&ring, &cqe); ec < 0) {
      die("Fail to wait cqe, errno {}", ec);
    }
    if (cqe->res == -1) {
      die("Fail to read payload");
    }
    std::cout << std::format("read {} bytes", cqe->res) << std::endl;
    std::cout << Hexdump(out_buffer.data(), cqe->res);
    auto resp = Payload{};
    auto deserializer = zpp::bits::in(out_buffer);
    deserializer(resp).or_throw();
    io_uring_cqe_seen(&ring, cqe);
    return resp;
  }

 private:
  Buffer &get_buffer() {
    idx = (idx + 1) % buffers.size();
    buffers[idx].clear();
    return buffers[idx];
  }

  io_uring ring;
  int sock = -1;
  Buffers buffers;
  uint32_t idx = 0;
};

struct PayloadType {
  uint32_t id;
  std::string message;
};