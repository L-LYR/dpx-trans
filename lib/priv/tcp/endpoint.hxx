#pragma once

#include <liburing.h>

#include <iostream>

#include "concept/rpc.hxx"
#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"
#include "util/fatal.hxx"
#include "util/hex_dump.hxx"
#include "util/logger.hxx"

namespace tcp {

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;

 public:
  Endpoint(size_t n_qe, size_t max_payload_size);
  ~Endpoint();

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
    auto &in = get_send_buffer();
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
    auto &out = get_recv_buffer();
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

  template <Rpc Rpc>
  resp_t<Rpc> call(req_t<Rpc> &&r) {
    write(std::forward<req_t<Rpc>>(r));
    return read<resp_t<Rpc>>();
  }

  template <Rpc Rpc>
  void serve() {
    // TODO: dispatch multiple rpcs
    write(Rpc::handler(read<resp_t<Rpc>>()));
  }

 private:
  // TODO better one
  Buffer &get_send_buffer();
  Buffer &get_recv_buffer();

  ConnectionPtr conn = nullptr;
  io_uring ring;
  Buffers buffers;
  uint32_t active_send_buffer_idx = 0;
  uint32_t active_recv_buffer_idx = 0;
};

}  // namespace tcp
