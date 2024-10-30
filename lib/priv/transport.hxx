#pragma once

#include "concept/rpc.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"
#include "util/hex_dump.hxx"
#include "util/logger.hxx"
#include "util/serialization.hxx"

// currently, we donot care the concurrent race.
class ReuseBuffers : public Buffers {
 public:
  ReuseBuffers(uint32_t n, uint32_t piece_size) : Buffers(n, piece_size) {}
  ~ReuseBuffers() = default;

  std::tuple<BorrowedBuffer &, size_t, BorrowedBuffer &, size_t> get_buffer_pair() {
    auto in_buf_idx = std::exchange(next_buffer_idx, (next_buffer_idx + 1) % (size() / 2));
    auto out_buf_idx = in_buf_idx + (size() / 2);
    auto &in_buf = (*this)[in_buf_idx];
    auto &out_buf = (*this)[out_buf_idx];
    in_buf.clear();
    out_buf.clear();
    return {in_buf, in_buf_idx, out_buf, out_buf_idx};
  }

 private:
  uint32_t next_buffer_idx = 0;
};

struct TcpConnectionInfo {
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = 0;
  uint16_t local_port = 0;
};

template <Backend b, bool passive>
class Transport {
  using CtrlPathEndpointType = std::conditional_t<b == Backend::TCP, tcp::Endpoint, void>;
  using CtrlPathConnector = std::conditional_t<b == Backend::TCP, tcp::Connector, void>;
  using CtrlPathAcceptor = std::conditional_t<b == Backend::TCP, tcp::Acceptor, void>;
  using DataPathEndpointType = std::conditional_t<b == Backend::TCP, tcp::Endpoint, void>;

  template <Backend, bool>
  friend class TransportGuard;

 public:
  Transport(uint32_t n_workers_, uint32_t max_rpc_msg_size, const TcpConnectionInfo &info)
    requires(b == Backend::TCP)
      : n_workers(n_workers_), buffers(n_workers * 2, max_rpc_msg_size) {
    ctrl_e.prepare(buffers);
    if constexpr (passive) {
      CtrlPathAcceptor(info.local_ip, info.local_port).associate({ctrl_e}).listen_and_accept();
    } else {
      CtrlPathConnector(info.remote_ip, info.remote_port).connect(ctrl_e, info.local_ip, info.local_port);
    }
    ctrl_e.run();
  }

  ~Transport() {
    if constexpr (!passive) {
      ctrl_e.stop();
    }
  }

  template <Rpc Rpc>
  resp_t<Rpc> call(req_t<Rpc> &&r)
    requires(!passive);

  template <Rpc... Rpcs>
  void serve()
    requires(passive);

 private:
  template <Rpc... Rpcs>
  void serve_once()
    requires(passive);

  size_t n_workers = -1;
  ReuseBuffers buffers;
  CtrlPathEndpointType ctrl_e;
};

template <Backend b, bool passive>
class TransportGuard : Noncopyable, Nonmovable {
 public:
  TransportGuard(Transport<b, passive> &t_) : t(t_) {
    // TODO add a futex to fast detect race and then die.
    poller = boost::fibers::fiber([this]() {
      while (t.ctrl_e.running() && !exit) {
        if (auto n = t.ctrl_e.progress(); n == 0) {
          boost::this_fiber::sleep_for(100ns);
        }
      }
    });
    TRACE("Attach to current thread");
  }
  ~TransportGuard() {
    exit = true;
    poller.join();
    TRACE("Dettach with current thread");
  }

 private:
  bool exit = false;
  Transport<b, passive> &t;
  boost::fibers::fiber poller;
};

template <Backend b, bool passive>
template <Rpc Rpc>
resp_t<Rpc> Transport<b, passive>::call(req_t<Rpc> &&r)
  requires(!passive)
{
  auto [in_buf, in_buf_idx, out_buf, out_buf_idx] = buffers.get_buffer_pair();
  auto serializer = Serializer(in_buf);
  serializer(Rpc::id, r).or_throw();

  TRACE("{}", Hexdump(in_buf.data(), serializer.position()));

  auto n_write = ctrl_e.post_send(in_buf, serializer.position()).get();
  if (n_write < 0) {
    die("Fail to write payload, errno: {}", -n_write);
  }
  auto n_read = ctrl_e.post_recv(out_buf, out_buf.size()).get();
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

namespace {

template <Rpc Rpc>
bool dispatch(rpc_id_t id, Deserializer &deserializer, Serializer &serializer) {
  if (Rpc::id == id) {
    req_t<Rpc> req = {};
    deserializer(req).or_throw();
    resp_t<Rpc> resp = Rpc::handler(req);
    serializer(Rpc::id, resp).or_throw();
    return true;
  }
  return false;
}

}  // namespace

template <Backend b, bool passive>
template <Rpc... Rpcs>
void Transport<b, passive>::serve_once()
  requires(passive)
{
  // constexpr auto n = sizeof...(Rpcs);
  // constexpr uint64_t ids[] = {Rpcs::id...};

  auto [in_buf, in_buf_idx, out_buf, out_buf_idx] = buffers.get_buffer_pair();

  TRACE("post recv");
  auto n_read = ctrl_e.post_recv(out_buf, out_buf.size()).get();
  if (n_read < 0) {
    die("Fail to read payload, errno: {}", -n_read);
  }

  if (n_read == 0) {
    // connection is closed
    if (ctrl_e.running()) {
      INFO("Connection was closed by peer, going to stop.");
      ctrl_e.stop();
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

  auto n_write = ctrl_e.post_send(in_buf, serializer.position()).get();
  if (n_write < 0) {
    die("Fail to write payload, errno: {}", -n_write);
  }

  TRACE("n_write: {}", n_write);
}

template <Backend b, bool passive>
template <Rpc... Rpcs>
void Transport<b, passive>::serve()
  requires(passive)
{
  TransportGuard g(*this);
  std::vector<boost::fibers::fiber> workers;
  workers.reserve(n_workers);
  for (auto i = 0uz; i < workers.capacity(); ++i) {
    workers.emplace_back([this]() {
      while (ctrl_e.running()) {
        serve_once<Rpcs...>();
      }
    });
  }
  for (auto &worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}