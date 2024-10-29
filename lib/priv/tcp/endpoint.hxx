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

template <Side side>
class Endpoint : public EndpointBase {
  friend class Acceptor;
  friend class Connector;
  friend class Connection;

 public:
  Endpoint(size_t n_qe, size_t max_payload_size);
  ~Endpoint();

  void prepare() {
    EndpointBase::prepare();
    if (buffers.empty()) {
      die("No buffer to use");
    }
    if (auto ec = io_uring_queue_init(buffers.size(), &ring, 0); ec < 0) {
      die("Fail to init ring, errno: {}", -ec);
    }
    std::vector<iovec> iovecs = buffers.to_iovec();
    if (auto ec = io_uring_register_buffers(&ring, iovecs.data(), iovecs.size()); ec < 0) {
      die("Fail to register buffers, errno: {}", -ec);
    }
  }
  void run() {
    EndpointBase::run();
    poller = boost::fibers::fiber([this]() {
      while (running()) {
        if (auto n = poll(1); n == 0) {
          boost::this_fiber::sleep_for(100ns);
        }
      }
    });
  }
  void stop() { EndpointBase::stop(); }

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
  [[nodiscard]] boost::fibers::future<int> post(BorrowedBuffer &buf, size_t nbytes, size_t idx);

  std::tuple<BorrowedBuffer &, size_t, BorrowedBuffer &, size_t> get_buffer_pair();

  int sock = -1;
  io_uring ring;
  Buffers<> buffers;
  boost::fibers::fiber poller;
  std::vector<boost::fibers::fiber> fibers;
  std::vector<boost::fibers::promise<int>> result_ps;
  uint32_t active_buffer_idx = 0;
};

template <Side side>
Endpoint<side>::Endpoint(size_t n_qe, size_t max_payload_size) : buffers(n_qe, max_payload_size), result_ps(n_qe) {
  assert(n_qe != 0 && n_qe % 2 == 0);
}

template <Side side>
Endpoint<side>::~Endpoint() {
  assert(stopped());
  if (sock != -1) {
    if (auto ec = ::close(sock); ec < 0) {
      die("Fail to close socket {}, errno: {}", sock, errno);
    }
  }
  if (poller.joinable()) {
    poller.join();
  }
  for (auto &fiber : fibers) {
    if (fiber.joinable()) {
      fiber.join();
    }
  }
  if (auto ec = io_uring_unregister_buffers(&ring); ec < 0) {
    die("Fail to unregister buffers, errno: {}", -ec);
  }
  io_uring_queue_exit(&ring);
};

template <Side side>
size_t Endpoint<side>::poll(size_t n) {
  io_uring_cqe *cqes = nullptr;
  auto got_n = io_uring_peek_batch_cqe(&ring, &cqes, n);
  for (auto &cqe : std::span(cqes, got_n)) {
    auto idx = io_uring_cqe_get_data64(&cqe);
    result_ps[idx].set_value(cqe.res);
    io_uring_cqe_seen(&ring, &cqe);
  }
  return got_n;
}

template <Side side>
std::tuple<BorrowedBuffer &, size_t, BorrowedBuffer &, size_t> Endpoint<side>::get_buffer_pair() {
  auto in_buf_idx = std::exchange(active_buffer_idx, (active_buffer_idx + 1) % (buffers.size() / 2));
  auto out_buf_idx = in_buf_idx + (buffers.size() / 2);
  buffers[in_buf_idx].clear();
  buffers[out_buf_idx].clear();
  return {buffers[in_buf_idx], in_buf_idx, buffers[out_buf_idx], out_buf_idx};
}

template <Side side>
template <Rpc Rpc>
resp_t<Rpc> Endpoint<side>::call(req_t<Rpc> &&r) {
  auto [in_buf, in_buf_idx, out_buf, out_buf_idx] = get_buffer_pair();
  auto serializer = Serializer(in_buf);
  serializer(Rpc::id, r).or_throw();

  TRACE("{}", Hexdump(in_buf.data(), serializer.position()));

  auto n_write = post<Op::Write>(in_buf, serializer.position(), in_buf_idx).get();
  if (n_write < 0) {
    die("Fail to write payload, errno: {}", -n_write);
  }
  auto n_read = post<Op::Read>(out_buf, out_buf.size(), out_buf_idx).get();
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

template <Side side>
template <Rpc Rpc>
bool Endpoint<side>::dispatch(rpc_id_t id, Deserializer &deserializer, Serializer &serializer) {
  if (Rpc::id == id) {
    req_t<Rpc> req = {};
    deserializer(req).or_throw();
    resp_t<Rpc> resp = Rpc::handler(req);
    serializer(Rpc::id, resp).or_throw();
    return true;
  }
  return false;
}

template <Side side>
template <Rpc... Rpcs>
void Endpoint<side>::serve_once() {
  // constexpr auto n = sizeof...(Rpcs);
  // constexpr uint64_t ids[] = {Rpcs::id...};

  auto [in_buf, in_buf_idx, out_buf, out_buf_idx] = get_buffer_pair();

  auto n_read = post<Op::Read>(out_buf, out_buf.size(), out_buf_idx).get();
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

  auto n_write = post<Op::Write>(in_buf, serializer.position(), in_buf_idx).get();
  if (n_write < 0) {
    die("Fail to write payload, errno: {}", -n_write);
  }

  TRACE("n_write: {}", n_write);
}

template <Side side>
template <Rpc... Rpcs>
void Endpoint<side>::serve(size_t n_fiber) {
  assert(n_fiber > 0 && n_fiber <= buffers.size());
  fibers.reserve(n_fiber);
  for (auto i = 0uz; i < n_fiber; ++i) {
    fibers.emplace_back([this]() {
      while (running()) {
        serve_once<Rpcs...>();
      }
    });
  }
}

template <Side side>
template <Endpoint<side>::Op op>
[[nodiscard]] boost::fibers::future<int> Endpoint<side>::post(BorrowedBuffer &buf, size_t nbytes, size_t idx) {
  assert(nbytes > 0 && nbytes <= buf.size());
  auto sqe = io_uring_get_sqe(&ring);
  if constexpr (op == Op::Write) {
    io_uring_prep_write_fixed(sqe, sock, buf.data(), nbytes, 0, idx);
  } else if constexpr (op == Op::Read) {
    io_uring_prep_read_fixed(sqe, sock, buf.data(), nbytes, 0, idx);
  } else {
    static_assert(false, "Wrong Op");
  }
  auto result_p = boost::fibers::promise<int>();
  auto result_f = result_p.get_future();
  io_uring_sqe_set_data64(sqe, idx);
  if (auto ec = io_uring_submit(&ring); ec < 0) {
    die("Fail to submit sqe, errno: {}", -ec);
  }
  result_ps[idx] = std::move(result_p);
  return result_f;
}

}  // namespace tcp
