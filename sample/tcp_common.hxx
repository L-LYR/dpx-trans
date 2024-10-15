#pragma once

#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "util/fatal.hxx"

class Connector {
 public:
  enum class Mode {
    ClientSide,
    ServerSide,
  };

  Connector(std::string remote_ip_, uint16_t remote_port_, std::string local_ip_, uint16_t local_port_)
      : mode(Mode::ClientSide),
        remote_ip(remote_ip_),
        local_ip(local_ip_),
        remote_port(remote_port_),
        local_port(local_port_) {}
  Connector(std::string local_ip_, uint16_t local_port_)
      : mode(Mode::ServerSide), local_ip(local_ip_), local_port(local_port_) {}
  ~Connector() {}

  // make uncopyable but movable

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
  void do_listen() {
    // create socket
    assert(mode == Mode::ServerSide);
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
      close(cqe->res);
      io_uring_cqe_seen(&ring, cqe);
    } while (true);
  }

  // client side
  void do_connect() {
    assert(mode == Mode::ClientSide);
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
    close(sock);
  }

 private:
  Mode mode;
  int sock = -1;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = -1;
  uint16_t local_port = -1;
};
