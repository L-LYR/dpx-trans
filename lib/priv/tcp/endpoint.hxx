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
#include "util/serialization.hxx"

using namespace std::chrono_literals;

namespace tcp {

class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;

 public:
  Endpoint(size_t n_qe, size_t max_payload_size);
  ~Endpoint();

  void run();

  template <Rpc Rpc>
  resp_t<Rpc> call(req_t<Rpc> &&r);

  template <Rpc... Rpcs>
  void serve(size_t n_fiber);

 private:
  enum class Op {
    Write,
    Read,
  };

  size_t poll(size_t n);

  template <Rpc Rpc>
  bool dispatch(rpc_id_t id, Deserializer &deserializer, Serializer &serializer);

  template <Rpc... Rpcs>
  void serve_once();

  template <Op op>
  [[nodiscard]] boost::fibers::future<int> post(Buffer &buf, size_t nbytes);

  std::pair<Buffer &, Buffer &> get_buffer_pair();

  ConnectionPtr conn = nullptr;
  io_uring ring;
  Buffers buffers;
  boost::fibers::fiber poller;
  std::vector<boost::fibers::fiber> fibers;
  std::vector<boost::fibers::promise<int>> result_ps;
  uint32_t active_buffer_idx = 0;
};

template <Rpc Rpc>
resp_t<Rpc> Endpoint::call(req_t<Rpc> &&r) {
  assert(conn->side == Side::ClientSide);

  auto [in_buf, out_buf] = get_buffer_pair();
  auto serializer = Serializer(in_buf);
  serializer(Rpc::id, r).or_throw();

  TRACE("{}", Hexdump(in_buf.data(), serializer.position()));

  auto n_write = post<Op::Write>(in_buf, serializer.position()).get();
  if (n_write < 0) {
    die("Fail to write payload, errno: {}", -n_write);
  }
  auto n_read = post<Op::Read>(out_buf, out_buf.size()).get();
  if (n_read < 0) {
    die("Fail to read payload, errno: {}", -n_read);
  }

  TRACE("{}", Hexdump(out_buf.data(), n_read));

  auto deserializer = Deserializer(out_buf);
  rpc_id_t id = 0;
  resp_t<Rpc> resp = {};
  deserializer(id, resp).or_throw();
  if (id != Rpc::id) {
    die("Mismatch rpc id, expected {} but got {}", Rpc::id, id);
  }
  return resp;
}

template <Rpc Rpc>
bool Endpoint::dispatch(rpc_id_t id, Deserializer &deserializer, Serializer &serializer) {
  if (Rpc::id == id) {
    req_t<Rpc> req = {};
    deserializer(req).or_throw();
    resp_t<Rpc> resp = Rpc::handler(req);
    serializer(Rpc::id, resp).or_throw();
    return true;
  }
  TRACE("expected {} got {}", Rpc::id, id);
  return false;
}

template <Rpc... Rpcs>
void Endpoint::serve_once() {
  // constexpr auto n = sizeof...(Rpcs);
  // constexpr uint64_t ids[] = {Rpcs::id...};

  auto [in_buf, out_buf] = get_buffer_pair();

  auto n_read = post<Op::Read>(out_buf, out_buf.size()).get();
  if (n_read < 0) {
    die("Fail to read payload, errno: {}", -n_read);
  }

  if (n_read == 0) {
    // connection is closed
    if (running()) {
      INFO("Connection was closed by peer, going to stop.");
      stop();
    }
    return;
  }

  TRACE("n_read: {}", n_read);

  auto deserializer = Deserializer(out_buf);

  rpc_id_t id = 0;
  deserializer(id).or_throw();

  TRACE("id: {}", id);

  auto serializer = Serializer(in_buf);

  if (!(dispatch<Rpcs>(id, deserializer, serializer) || ...)) {
    die("Mismatch rpc id, got {}", id);
  }

  auto n_write = post<Op::Write>(in_buf, serializer.position()).get();
  if (n_write < 0) {
    die("Fail to write payload, errno: {}", -n_write);
  }

  TRACE("n_write: {}", n_write);
}

template <Rpc... Rpcs>
void Endpoint::serve(size_t n_fiber) {
  assert(conn->side == Side::ServerSide);
  assert(n_fiber > 0 && n_fiber <= buffers.size());

  run();
  fibers.reserve(n_fiber);
  for (auto i = 0uz; i < n_fiber; ++i) {
    fibers.emplace_back([this]() {
      while (running()) {
        serve_once<Rpcs...>();
      }
    });
  }
}

template <Endpoint::Op op>
[[nodiscard]] boost::fibers::future<int> Endpoint::post(Buffer &buf, size_t nbytes) {
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

}  // namespace tcp
