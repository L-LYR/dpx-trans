#pragma once

#include <list>

#include "concept/rpc.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"
#include "util/logger.hxx"
#include "util/serialization.hxx"

using BorrowedBufferRef = std::reference_wrapper<BorrowedBuffer>;
using BorrowedBufferRefQueue = std::list<BorrowedBufferRef>;

struct ConnectionInfo {
  bool passive;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = 0;
  uint16_t local_port = 0;
};

template <Backend b, Rpc... rpcs>
class Transport {
  using CtrlPathEndpointType = std::conditional_t<b == Backend::TCP, tcp::Endpoint, void>;
  using CtrlPathConnector = std::conditional_t<b == Backend::TCP, tcp::Connector, void>;
  using CtrlPathAcceptor = std::conditional_t<b == Backend::TCP, tcp::Acceptor, void>;
  using DataPathEndpointType = std::conditional_t<b == Backend::TCP, tcp::Endpoint, void>;

  template <Backend, Rpc...>
  friend class TransportGuard;

 public:
  Transport(uint32_t n_workers_, uint32_t max_rpc_msg_size, const ConnectionInfo &info_)
    requires(b == Backend::TCP)
      : info(info_), n_workers(n_workers_), buffers(n_workers, max_rpc_msg_size) {
    ctrl_e.prepare(buffers);
    for (auto &buffer : buffers) {
      vacant_buffers.push_back(buffer);
    }
    if (info.passive) {
      CtrlPathAcceptor(info.local_ip, info.local_port).associate({ctrl_e}).listen_and_accept();
    } else {
      CtrlPathConnector(info.remote_ip, info.remote_port).connect(ctrl_e, info.local_ip, info.local_port);
    }
    ctrl_e.run();
  }

  ~Transport() {
    if (!info.passive) {
      ctrl_e.stop();
    }
  }

  template <Rpc Rpc>
  resp_future_t<Rpc> call(const req_t<Rpc> &r) {
    auto call_seq = seq++;

    while (vacant_buffers.empty()) {
      boost::this_fiber::yield();
    }
    auto &buf = vacant_buffers.front().get();
    vacant_buffers.pop_front();

    auto serializer = Serializer(buf);
    serializer(call_seq, Rpc::id, r).or_throw();

    TRACE("send with seq: {} id: {}", call_seq, Rpc::id);
    OpContext op_ctx;
    TRACE("caller post write");
    auto n_write = ctrl_e.post_send(op_ctx, buf).get();
    if (n_write <= 0) {
      die("Fail to write payload, errno: {}", -n_write);
    }
    TRACE("caller write {}", n_write);

    auto rpc_ctx = new RpcContext<Rpc>;
    resp_future_t<Rpc> f = rpc_ctx->resp.get_future();

    [[maybe_unused]] auto [_, ok] = outstanding_rpcs.emplace(call_seq, rpc_ctx);
    assert(ok);

    boost::fibers::fiber([this, &buf]() {
      OpContext op_ctx;
      TRACE("caller post recv");
      auto n_read = ctrl_e.post_recv(op_ctx, buf).get();
      if (n_read <= 0) {
        die("Fail to read payload, errno: {}", -n_read);
      }
      TRACE("caller read {}", n_read);
      auto deserializer = Deserializer(buf);
      int64_t seq = 0;
      rpc_id_t id = 0;
      deserializer(seq, id).or_throw();
      TRACE("recv with seq: {} id: {}", seq, id);
      if (seq >= 0) {
        die("Payload is not response");
      }
      if (!(dispatch_response<rpcs>(-seq, id, deserializer) || ...)) {
        die("Mismatch rpc id, got {}", id);
      }
      vacant_buffers.push_back(buf);
    }).detach();

    return f;
  }

  void serve() {
    std::vector<boost::fibers::fiber> workers;
    workers.reserve(n_workers);
    for (auto i = 0uz; i < workers.capacity(); ++i) {
      workers.emplace_back([this, i]() {
        while (ctrl_e.running()) {
          serve_once(i);
        }
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
  }

 private:
  template <Rpc Rpc>
  bool dispatch_response(int64_t seq, rpc_id_t id, Deserializer &deserializer) {
    if (Rpc::id == id) {
      resp_t<Rpc> resp = {};
      deserializer(resp).or_throw();
      auto iter = outstanding_rpcs.find(seq);
      assert(iter != outstanding_rpcs.end());
      auto ctx = iter->second;
      static_cast<RpcContext<Rpc> *>(ctx)->resp.set_value(resp);
      outstanding_rpcs.erase(iter);
      delete ctx;
      return true;
    }
    return false;
  }

  template <Rpc Rpc>
  bool dispatch_request(int64_t seq, rpc_id_t id, Deserializer &deserializer, Serializer &serializer) {
    if (Rpc::id == id) {
      req_t<Rpc> req = {};
      deserializer(req).or_throw();
      resp_t<Rpc> resp = Rpc()(req);
      TRACE("send seq: {}, id: {}", -seq, id);
      serializer(-seq, Rpc::id, resp).or_throw();
      return true;
    }
    return false;
  }

  void serve_once(size_t idx) {
    // constexpr auto n = sizeof...(Rpcs);
    // constexpr uint64_t ids[] = {Rpcs::id...};

    auto &buf = buffers[idx];
    buf.clear();

    {
      TRACE("worker {} post recv", idx);
      OpContext ctx;
      auto n_read = ctrl_e.post_recv(ctx, buf).get();
      if (n_read < 0) {
        die("Fail to read payload, errno: {}", -n_read);
      } else if (n_read == 0) {
        // TODO add disconnect event
        if (ctrl_e.running()) {
          TRACE("Connection was closed by peer, going to exit");
          ctrl_e.stop();
        }
        return;
      }
      TRACE("worker {} read: {}", idx, n_read);
    }

    auto deserializer = Deserializer(buf);

    int64_t seq = 0;
    rpc_id_t id = 0;

    deserializer(seq, id).or_throw();

    TRACE("recv seq: {} id: {}", seq, id);
    if (seq < 0) {
      die("Payload is not request");
    }

    auto serializer = Serializer(buf);
    if (!(dispatch_request<rpcs>(seq, id, deserializer, serializer) || ...)) {
      die("Mismatch rpc id, got {}", id);
    }

    {
      TRACE("worker {} post send", idx);
      OpContext ctx;
      auto n_write = ctrl_e.post_send(ctx, buf).get();
      if (n_write < 0) {
        die("Fail to write payload, errno: {}", -n_write);
      }
      TRACE("worker {} write: {}", idx, n_write);
    }
  }

  bool progress() { return ctrl_e.progress(); }

  ConnectionInfo info;

  size_t n_workers = -1;
  int64_t seq = 1;
  Buffers buffers;

  BorrowedBufferRefQueue vacant_buffers;
  std::unordered_map<int64_t, ContextBase *> outstanding_rpcs;

  CtrlPathEndpointType ctrl_e;
  DataPathEndpointType data_e;
};

template <Backend b, Rpc... rpcs>
class TransportGuard : Noncopyable, Nonmovable {
 public:
  TransportGuard(Transport<b, rpcs...> &t_) : t(t_) {
    // TODO add a futex to fast detect race and then die.
    poller = boost::fibers::fiber([this]() {
      while (!(t.outstanding_rpcs.empty() && exit)) {
        if (!t.progress()) {
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
  Transport<b, rpcs...> &t;
  boost::fibers::fiber poller;
};
