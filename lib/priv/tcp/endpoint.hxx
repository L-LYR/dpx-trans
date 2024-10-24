#pragma once

#include <liburing.h>

#include <boost/fiber/future.hpp>

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
  friend class Connection;

 public:
  Endpoint(size_t n_qe, size_t max_payload_size);
  ~Endpoint();

  size_t poll(size_t n);

  template <Rpc Rpc>
  resp_t<Rpc> call(req_t<Rpc> &&r) {
    auto [in_buf, out_buf] = get_buffer_pair();
    auto serializer = zpp::bits::out(in_buf);
    serializer(Rpc::id, r).or_throw();

    TRACE("\n{}", Hexdump(in_buf.data(), serializer.position()));

    auto n_write = post<Op::Write>(in_buf, serializer.position()).get();
    if (n_write < 0) {
      die("Fail to write payload, errno: {}", -n_write);
    }
    auto n_read = post<Op::Read>(out_buf, out_buf.size()).get();
    if (n_read < 0) {
      die("Fail to read payload, errno: {}", -n_read);
    }

    TRACE("\n{}", Hexdump(out_buf.data(), n_read));

    auto deserializer = zpp::bits::in(out_buf);
    rpc_id_t id = 0;
    resp_t<Rpc> resp = {};
    deserializer(id, resp).or_throw();
    if (id != Rpc::id) {
      die("Mismatch rpc id, expected {} but got {}", Rpc::id, id);
    }
    return resp;
  }

  template <Rpc... Rpcs>
  void serve_once() {
    // constexpr auto n = sizeof...(Rpcs);
    // constexpr uint64_t ids[] = {Rpcs::id...};

    auto [in_buf, out_buf] = get_buffer_pair();

    auto n_read = post<Op::Read>(out_buf, out_buf.size()).get();
    if (n_read < 0) {
      die("Fail to read payload, errno: {}", -n_read);
    }

    auto deserializer = zpp::bits::in(out_buf);

    rpc_id_t id = 0;
    deserializer(id).or_throw();

    auto serializer = zpp::bits::out(in_buf);

    auto fn = [&]<Rpc Rpc>() -> void {
      if (id == Rpc::id) {
        req_t<Rpc> req = {};
        deserializer(req).or_throw();
        resp_t<Rpc> resp = Rpc::handler(req);
        serializer(Rpc::id, resp).or_throw();
      }
    };

    (fn.template operator()<Rpcs>(), ...);

    auto n_write = post<Op::Write>(in_buf, serializer.position()).get();
    if (n_write < 0) {
      die("Fail to write payload, errno: {}", -n_write);
    }
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
  std::pair<Buffer &, Buffer &> get_buffer_pair();

  ConnectionPtr conn = nullptr;
  io_uring ring;
  Buffers buffers;
  std::vector<boost::fibers::promise<int>> result_ps;
  uint32_t active_buffer_idx = 0;
};

}  // namespace tcp
