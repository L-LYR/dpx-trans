#pragma once

#include "concept/rpc.hxx"
#include "memory/simple_buffer.hxx"
#include "priv/common.hxx"
#include "priv/doca_comch/connection.hxx"
#include "priv/doca_comch/endpoint.hxx"
#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"
#include "priv/verbs/connection.hxx"
#include "priv/verbs/endpoint.hxx"
#include "util/fatal.hxx"
#include "util/logger.hxx"
#include "util/serialization.hxx"

// clang-format off
template<Backend b>
  using ConnectionParam =
    std::conditional_t<b == Backend::TCP, tcp::ConnectionParam,
    std::conditional_t<b == Backend::Verbs, verbs::ConnectionParam,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::ConnectionParam, void>>>;
// clang-format on

template <Backend b, Rpc... rpcs>
class Transport {
  // clang-format off
  using CtrlPathEndpointType =
    std::conditional_t<b == Backend::TCP, tcp::Endpoint,
    std::conditional_t<b == Backend::Verbs, verbs::Endpoint,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::ctrl_path::Endpoint, void>>>;
  using ConnectionHandleType =
    std::conditional_t<b == Backend::TCP, tcp::ConnectionHandle,
    std::conditional_t<b == Backend::Verbs, verbs::ConnectionHandle,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::ctrl_path::ConnectionHandle, void>>>;
  // clang-format on
  using ConnectionParam = ConnectionParam<b>;

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
    establish_connections();
  }

  Transport(doca::Device &dev, uint32_t n_workers_, uint32_t max_rpc_msg_size,
            const doca::comch::ConnectionParam &param)
    requires(b == Backend::DOCA_Comch)
      : n_workers(n_workers_),
        param(param),
        conn_handle(param),
        buffers(n_workers * 2, max_rpc_msg_size),
        ctrl_e(dev, buffers, param.name) {
    establish_connections();
  }

  ~Transport() { terminate_connections(); }

  template <Rpc Rpc>
  resp_future_t<Rpc> call(const req_t<Rpc> &r) {
    auto call_seq = seq++;
    {
      TRACE("caller post recv");
      auto recv_ctx = new OpContext(Op::Recv, acquire_buffer());
      auto n_read_f = ctrl_e.post_recv(*recv_ctx);
      boost::fibers::fiber([this, n_read_f = std::move(n_read_f), recv_ctx]() mutable {
        auto n_read = n_read_f.get();
        if (n_read <= 0) {
          die("Fail to read payload, errno: {}", -n_read);
        }
        TRACE("caller read {}", n_read);
        auto deserializer = Deserializer(recv_ctx->buf);
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
        release_buffer(std::move(recv_ctx->buf));
        delete recv_ctx;
      }).detach();
    }

    auto send_buf = acquire_buffer();
    auto serializer = Serializer(send_buf);
    serializer(call_seq, Rpc::id, r).or_throw();
    TRACE("caller post write");
    OpContext send_ctx(Op::Send, std::move(send_buf), serializer.position());
    auto n_write_f = ctrl_e.post_send(send_ctx);
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
    release_buffer(std::move(send_ctx.buf));

    return f;
  }

  void serve() {
    std::vector<boost::fibers::fiber> workers;
    workers.reserve(n_workers);
    for (auto i = 0uz; i < workers.capacity(); ++i) {
      workers.emplace_back([this, i]() {
        active_workers++;
        while (ctrl_e.running()) {
          serve_once(i);
        }
        active_workers--;
      });
    }
    for (auto &worker : workers) {
      worker.join();
    }
  }

 private:
  void terminate_connections() {
    if (param.passive) {
      if constexpr (b == Backend::DOCA_Comch) {
        conn_handle.wait_for_disconnect();
      }
    } else {
      conn_handle.disconnect();
    }
  }
  void establish_connections() {
    conn_handle.associate(ctrl_e);
    if (param.passive) {
      conn_handle.listen_and_accept();
      if constexpr (b == Backend::Verbs || b == Backend::TCP) {
        std::thread([this]() { conn_handle.wait_for_disconnect(); }).detach();
      }
    } else {
      conn_handle.connect();
    }
  }

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

    if (!ctrl_e.running()) {
      TRACE("not running");
      return;
    }

    auto buf = acquire_buffer();
    buf.clear();

    TRACE("worker {} post recv", idx);
    OpContext recv_ctx(Op::Recv, std::move(buf));
    auto n_read = ctrl_e.post_recv(recv_ctx).get();
    if (n_read < 0) {
      die("Fail to read payload, errno: {}", -n_read);
    } else if (n_read == 0) {
      TRACE("closed");
      release_buffer(std::move(recv_ctx.buf));
      return;
    }
    TRACE("worker {} read: {}", idx, n_read);

    auto deserializer = Deserializer(recv_ctx.buf);

    int64_t seq = 0;
    rpc_id_t id = 0;

    deserializer(seq, id).or_throw();

    TRACE("recv seq: {} id: {}", seq, id);
    if (seq < 0) {
      die("Payload is not request");
    }

    auto serializer = Serializer(recv_ctx.buf);
    if (!(dispatch_request<rpcs>(seq, id, deserializer, serializer) || ...)) {
      die("Mismatch rpc id, got {}", id);
    }

    TRACE("worker {} post send", idx);
    OpContext send_ctx(Op::Send, std::move(recv_ctx.buf), serializer.position());
    auto n_write = ctrl_e.post_send(send_ctx).get();
    if (n_write < 0) {
      die("Fail to write payload, errno: {}", -n_write);
    } else if (n_write == 0) {
      TRACE("closed");
      release_buffer(std::move(send_ctx.buf));
      return;
    }
    TRACE("worker {} write: {}", idx, n_write);

    release_buffer(std::move(send_ctx.buf));
  }

  BorrowedBuffer acquire_buffer() {
    while (true) {
      if (auto buf = buffers.acquire_one(); buf.has_value()) {
        return std::move(buf).value();
      } else {
        boost::this_fiber::yield();
      }
    }
  }

  void release_buffer(BorrowedBuffer &&buf) { buffers.release_one(std::move(buf)); }

  void progress_until(std::function<bool()> &&fn) {
    while (!(outstanding_rpcs.empty() && active_workers == 0 && fn())) {
      if (!ctrl_e.progress()) {
        boost::this_fiber::yield();
      }
    }
  }

  const size_t n_workers = -1;
  int64_t seq = 1;
  ConnectionParam param;
  ConnectionHandleType conn_handle;
  naive::Buffers buffers;
  CtrlPathEndpointType ctrl_e;
  std::unordered_map<int64_t, ContextBase *> outstanding_rpcs;
  uint32_t active_workers = 0;
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
