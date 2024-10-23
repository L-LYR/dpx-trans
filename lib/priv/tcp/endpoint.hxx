#pragma once

#include <liburing.h>

#include <boost/fiber/future.hpp>
#include <iostream>

#include "concept/rpc.hxx"
#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"
#include "util/fatal.hxx"
#include "util/hex_dump.hxx"

namespace tcp {

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;

 public:
  Endpoint(size_t n_qe, size_t max_payload_size);
  ~Endpoint();

  size_t poll(size_t n) {
    io_uring_cqe *cqes = nullptr;
    auto got_n = io_uring_peek_batch_cqe(&ring, &cqes, n);
    for (auto &cqe : std::span(cqes, got_n)) {
      auto idx = io_uring_cqe_get_data64(&cqe);
      result_ps[idx].set_value(cqe.res);
      io_uring_cqe_seen(&ring, &cqe);
    }
    return got_n;
  }

  template <typename Payload>
  void write(Payload &&payload) {
    auto &in = get_send_buffer();
    auto serializer = zpp::bits::out(in);
    serializer(payload).or_throw();
    std::cout << std::endl << Hexdump(in.data(), serializer.position()) << std::endl;
    auto n = post<Op::Write>(in, serializer.position()).get();
    if (n < 0) {
      die("Fail to write payload, errno: {}", -n);
    }
  }

  template <typename Payload>
  Payload read() {
    auto &out = get_recv_buffer();
    auto n = post<Op::Read>(out, out.size()).get();
    if (n < 0) {
      die("Fail to read payload, errno: {}", -n);
    }
    auto resp = Payload{};
    std::cout << std::endl << Hexdump(out.data(), n) << std::endl;
    auto deserializer = zpp::bits::in(out);
    deserializer(resp).or_throw();
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
    write(Rpc::handler(read<req_t<Rpc>>()));
  }

 private:
  enum class Op {
    Write,
    Read,
  };

  template <Op op>
  [[nodiscard]] boost::fibers::future<int> post(Buffer &buf, size_t nbytes) {
    assert(nbytes > 0 && nbytes <= buf.size());
    auto sqe = io_uring_get_sqe(&ring);
    if constexpr (op == Op::Write) {
      io_uring_prep_write_fixed(sqe, conn->sock, buf.data(), nbytes, 0, buf.index());
    } else if constexpr (op == Op::Read) {
      io_uring_prep_read_fixed(sqe, conn->sock, buf.data(), nbytes, 0, buf.index());
    } else {
      static_assert(false, "Wrong Op");
    }
    auto result_p = boost::fibers::promise<int>();
    auto result_f = result_p.get_future();
    io_uring_sqe_set_data64(sqe, buf.index());
    if (auto ec = io_uring_submit(&ring); ec < 0) {
      die("Fail to submit sqe, errno: {}", -ec);
    }
    result_ps[buf.index()] = std::move(result_p);
    return result_f;
  }

  // TODO better one
  Buffer &get_send_buffer();
  Buffer &get_recv_buffer();

  ConnectionPtr conn = nullptr;
  io_uring ring;
  Buffers buffers;
  std::vector<boost::fibers::promise<int>> result_ps;
  uint32_t active_send_buffer_idx = 0;
  uint32_t active_recv_buffer_idx = 0;
};

}  // namespace tcp
