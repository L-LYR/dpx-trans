#pragma once

#include <list>

#include "concept/rpc.hxx"
#include "priv/common.hxx"
#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"
// #include "priv/verbs/connection.hxx"
// #include "priv/verbs/endpoint.hxx"
#include "util/fatal.hxx"
#include "util/logger.hxx"
#include "util/serialization.hxx"

using BorrowedBufferRef = std::reference_wrapper<BorrowedBuffer>;
using BorrowedBufferRefQueue = std::list<BorrowedBufferRef>;

template <Backend b, Rpc... rpcs>
class Transport {
  using CtrlPathEndpointType = std::conditional_t<b == Backend::TCP, tcp::Endpoint, void>;
  using ConnectionHandleType = std::conditional_t<b == Backend::TCP, tcp::ConnectionHandle, void>;

  template <Backend, Rpc...>
  friend class TransportGuard;

 public:
  Transport(uint32_t n_workers_, uint32_t max_rpc_msg_size, const ConnectionParam &param)
    requires(b == Backend::TCP || b == Backend::Verbs)
      : n_workers(n_workers_),
        param(param),
        conn_handle(param),
        buffers(n_workers * 2, max_rpc_msg_size),
        ctrl_e(buffers) {
    ctrl_e.prepare();
    for (auto &buffer : buffers) {
      vacant_buffers.push_back(buffer);
    }
    conn_handle.associate(ctrl_e);
    if (param.passive) {
      conn_handle.listen_and_accept();
      std::thread([this]() { conn_handle.wait_for_disconnect(); }).detach();
    } else {
      conn_handle.connect();
    }
  }

  ~Transport() {
    if (!param.passive) {
      conn_handle.disconnect();
    }
  }

  template <Rpc Rpc>
  resp_future_t<Rpc> call(const req_t<Rpc> &r) {
    auto call_seq = seq++;
    {
      auto recv_ctx = new OpContext;
      TRACE("caller post recv");
      auto recv_buf = acquire_buffer();
      auto n_read_f = ctrl_e.post_recv(*recv_ctx, recv_buf);
      boost::fibers::fiber([this, recv_buf = std::move(recv_buf), n_read_f = std::move(n_read_f), recv_ctx]() mutable {
        auto n_read = n_read_f.get();
        if (n_read <= 0) {
          die("Fail to read payload, errno: {}", -n_read);
        }
        TRACE("caller read {}", n_read);
        auto deserializer = Deserializer(recv_buf);
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
        release_buffer(recv_buf);
        delete recv_ctx;
      }).detach();
    }

    OpContext send_ctx;
    auto send_buf = acquire_buffer();
    auto serializer = Serializer(send_buf);
    serializer(call_seq, Rpc::id, r).or_throw();
    TRACE("caller post write");
    auto n_write_f = ctrl_e.post_send(send_ctx, send_buf, serializer.position());
    TRACE("send with seq: {} id: {}", call_seq, Rpc::id);
    auto rpc_ctx = new RpcContext<Rpc>;
    resp_future_t<Rpc> f = rpc_ctx->resp.get_future();
    [[maybe_unused]] auto [_, ok] = outstanding_rpcs.emplace(call_seq, rpc_ctx);
    assert(ok);
    auto n_write = n_write_f.get();
    if (n_write <= 0) {
      die("Fail to write payload, errno: {}", -n_write);
    }
    TRACE("caller write {}", n_write);
    release_buffer(send_buf);

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
        TRACE("closed");
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
      auto n_write = ctrl_e.post_send(ctx, buf, serializer.position()).get();
      if (n_write < 0) {
        die("Fail to write payload, errno: {}", -n_write);
      }
      TRACE("worker {} write: {}", idx, n_write);
    }
  }

  BorrowedBufferRef acquire_buffer() {
    while (vacant_buffers.empty()) {
      boost::this_fiber::yield();
    }
    auto &buf = vacant_buffers.front().get();
    vacant_buffers.pop_front();
    return buf;
  }

  void release_buffer(const BorrowedBufferRef &buf) { vacant_buffers.push_back(buf); }

  void progress_until(std::function<bool()> &&fn) {
    while (!(outstanding_rpcs.empty() && fn())) {
      if (!ctrl_e.progress()) {
        boost::this_fiber::yield();
      }
    }
  }

  const size_t n_workers = -1;
  int64_t seq = 1;
  ConnectionParam param;
  ConnectionHandleType conn_handle;
  Buffers buffers;
  CtrlPathEndpointType ctrl_e;
  BorrowedBufferRefQueue vacant_buffers;
  std::unordered_map<int64_t, ContextBase *> outstanding_rpcs;
};

template <Backend b, Rpc... rpcs>
class TransportGuard : Noncopyable, Nonmovable {
 public:
  TransportGuard(Transport<b, rpcs...> &t_) : t(t_) {
    // TODO add a futex to fast detect race and then die.
    poller = boost::fibers::fiber([this]() { t.progress_until([this] { return exit; }); });
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
