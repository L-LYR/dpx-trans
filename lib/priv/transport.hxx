#pragma once

#include "concept/rpc.hxx"
#include "memory/simple_buffer_pool.hxx"
#include "priv/context.hxx"
#include "priv/defs.hxx"
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
    std::conditional_t<b == Backend::TCP,        tcp::ConnectionParam,
    std::conditional_t<b == Backend::Verbs,      verbs::ConnectionParam,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::ConnectionParam,
                                                 void>>>;
// clang-format on

template <Backend b>
struct Config {
  uint32_t queue_depth;
  uint32_t max_rpc_msg_size;
  ConnectionParam<b> conn_param;
};

template <Backend b, Side s, Rpc... rpcs>
class Transport {
  // clang-format off
  using Endpoint =
    std::conditional_t<b == Backend::TCP,        tcp::Endpoint,
    std::conditional_t<b == Backend::Verbs,      verbs::Endpoint,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::Endpoint<s>,
                                                 void>>>;

  using ConnHandle =
    std::conditional_t<b == Backend::TCP,        tcp::ConnectionHandle,
    std::conditional_t<b == Backend::Verbs,      verbs::ConnectionHandle,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::ConnectionHandle<s>,
                                                 void>>>;

  using ConnectionParam = ConnectionParam<b>;
  using RpcBufferPool=
    std::conditional_t<b == Backend::DOCA_Comch, BufferPool<doca::Buffers>,
                                                 BufferPool<naive::Buffers>>;
  using BulkBufferPool = RpcBufferPool;
  using RpcBuffer = RpcBufferPool::BufferType;
  // clang-format on

  template <Backend, Side, Rpc...>
  friend class TransportGuard;

 public:
  Transport(Config<b> config_)
    requires(b == Backend::TCP || b == Backend::Verbs)
      : config(config_),
        cp_conn_handle(config.conn_param),
        send_bufs(config.queue_depth, config.max_rpc_msg_size),
        recv_bufs(config.queue_depth, config.max_rpc_msg_size),
        cp_e(send_bufs.buffers(), recv_bufs.buffers()) {
    establish_connections();
  }

  Transport(doca::Device &dev, Config<b> config_)
    requires(b == Backend::DOCA_Comch)
      : config(config_),
        cp_conn_handle(config.conn_param),
        send_bufs(dev, config.queue_depth, config.max_rpc_msg_size),
        recv_bufs(dev, config.queue_depth, config.max_rpc_msg_size),
        cp_e(dev, send_bufs.buffers(), recv_bufs.buffers(), config.conn_param.name) {
    establish_connections();
  }

  ~Transport() { terminate_connections(); }

  template <Rpc Rpc>
  resp_future_t<Rpc> call(const req_t<Rpc> &r)
    requires(s == Side::ClientSide)
  {
    auto call_seq = seq++;
    {
      DEBUG("caller post recv");
      auto recv_ctx = new OpContext(Op::Recv, acquire_recv_buffer());
      auto n_read_f = cp_e.post_recv(*recv_ctx);
      boost::fibers::fiber([this, n_read_f = std::move(n_read_f), recv_ctx]() mutable {
        auto n_read = n_read_f.get();
        if (n_read <= 0) {
          die("Fail to read payload, errno: {}", -n_read);
        }
        DEBUG("caller read {}", n_read);
        auto deserializer = Deserializer(recv_ctx->buf);
        int64_t seq = 0;
        rpc_id_t id = 0;
        deserializer(seq, id).or_throw();
        DEBUG("recv with seq: {} id: {}", seq, id);
        if (seq >= 0) {
          die("Payload is not response");
        }
        if (!(dispatch_response<rpcs>(-seq, id, deserializer) || ...)) {
          die("Mismatch rpc id, got {}", id);
        }
        release_recv_buffer(recv_ctx->buf);
        delete recv_ctx;
      }).detach();
    }

    OpContext send_ctx(Op::Send, acquire_send_buffer());
    auto serializer = Serializer(send_ctx.buf);
    serializer(call_seq, Rpc::id, r).or_throw();
    DEBUG("caller post write {}", serializer.position());
    send_ctx.len = serializer.position();
    auto n_write_f = cp_e.post_send(send_ctx);
    DEBUG("send with seq: {} id: {}", call_seq, Rpc::id);
    auto rpc_ctx = new RpcContext<Rpc>;
    resp_future_t<Rpc> f = rpc_ctx->resp.get_future();
    [[maybe_unused]] auto [_, ok] = outstanding_rpcs.emplace(call_seq, rpc_ctx);
    assert(ok);
    auto n_write = n_write_f.get();
    if (n_write <= 0) {
      die("Fail to write payload, errno: {}", -n_write);
    }
    DEBUG("caller write {}", n_write);
    release_send_buffer(send_ctx.buf);

    return f;
  }

  void serve()
    requires(s == Side::ServerSide)
  {
    std::vector<boost::fibers::fiber> workers;
    workers.reserve(config.queue_depth);
    for (auto i = 0uz; i < workers.capacity(); ++i) {
      workers.emplace_back([this, i]() {
        active_workers++;
        while (cp_e.running()) {
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
    if constexpr (s == Side::ServerSide) {
      if constexpr (b == Backend::DOCA_Comch) {
        cp_conn_handle.wait_for_disconnect();
      }
    } else {
      cp_conn_handle.disconnect();
    }
  }

  void establish_connections() {
    cp_conn_handle.associate(cp_e);
    if constexpr (s == Side::ServerSide) {
      cp_conn_handle.listen_and_accept();
      if constexpr (b == Backend::Verbs || b == Backend::TCP) {
        std::thread([this]() { cp_conn_handle.wait_for_disconnect(); }).detach();
      }
    } else {
      cp_conn_handle.connect();
    }
  }

  template <Rpc Rpc>
  bool dispatch_response(int64_t seq, rpc_id_t id, Deserializer &deserializer)
    requires(s == Side::ClientSide)
  {
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
  bool dispatch_request(int64_t seq, rpc_id_t id, Deserializer &deserializer, Serializer &serializer)
    requires(s == Side::ServerSide)
  {
    if (Rpc::id == id) {
      req_t<Rpc> req = {};
      deserializer(req).or_throw();
      resp_t<Rpc> resp = Rpc()(req);
      DEBUG("send seq: {}, id: {}", -seq, id);
      serializer(-seq, Rpc::id, resp).or_throw();
      return true;
    }
    return false;
  }

  void serve_once([[maybe_unused]] size_t idx)
    requires(s == Side::ServerSide)
  {
    // constexpr auto n = sizeof...(Rpcs);
    // constexpr uint64_t ids[] = {Rpcs::id...};

    if (!cp_e.running()) {
      DEBUG("not running");
      return;
    }

    DEBUG("worker {} post recv", idx);
    OpContext recv_ctx(Op::Recv, acquire_recv_buffer());
    auto n_read = cp_e.post_recv(recv_ctx).get();
    if (n_read < 0) {
      die("Fail to read payload, errno: {}", -n_read);
    } else if (n_read == 0) {
      DEBUG("closed");
      release_recv_buffer(recv_ctx.buf);
      return;
    }
    DEBUG("worker {} read: {}", idx, n_read);
    auto deserializer = Deserializer(recv_ctx.buf);
    int64_t seq = 0;
    rpc_id_t id = 0;
    deserializer(seq, id).or_throw();
    DEBUG("recv seq: {} id: {}", seq, id);
    if (seq < 0) {
      die("Payload is not request");
    }

    OpContext send_ctx(Op::Send, acquire_send_buffer());
    auto serializer = Serializer(send_ctx.buf);
    if (!(dispatch_request<rpcs>(seq, id, deserializer, serializer) || ...)) {
      die("Mismatch rpc id, got {}", id);
    }
    release_recv_buffer(recv_ctx.buf);

    DEBUG("worker {} post send {}", idx, serializer.position());
    send_ctx.len = serializer.position();
    auto n_write = cp_e.post_send(send_ctx).get();
    if (n_write < 0) {
      die("Fail to write payload, errno: {}", -n_write);
    } else if (n_write == 0) {
      DEBUG("closed");
      release_send_buffer(send_ctx.buf);
      return;
    }
    DEBUG("worker {} write: {}", idx, n_write);

    release_send_buffer(send_ctx.buf);
  }

  BufferBase &acquire_recv_buffer() {
    while (true) {
      if (auto buf = recv_bufs.acquire_one(); buf.has_value()) {
        return buf.value();
      } else {
        boost::this_fiber::yield();
      }
    }
  }

  void release_recv_buffer(BufferBase &buf) { recv_bufs.release_one(static_cast<RpcBuffer &>(buf)); }

  BufferBase &acquire_send_buffer() {
    while (true) {
      if (auto buf = send_bufs.acquire_one(); buf.has_value()) {
        return buf.value();
      } else {
        boost::this_fiber::yield();
      }
    }
  }

  void release_send_buffer(BufferBase &buf) { send_bufs.release_one(static_cast<RpcBuffer &>(buf)); }

  void progress_until(std::function<bool()> &&predictor) {
    while (!(outstanding_rpcs.empty() && active_workers == 0 && predictor())) {
      if (!cp_e.progress()) {
        boost::this_fiber::yield();
      }
    }
  }

  const Config<b> config;
  ConnHandle cp_conn_handle;
  RpcBufferPool send_bufs;
  RpcBufferPool recv_bufs;
  Endpoint cp_e;

  int64_t seq = 1;
  std::unordered_map<int64_t, ContextBase *> outstanding_rpcs;
  uint32_t active_workers = 0;
};

template <Backend b, Side s, Rpc... rpcs>
class TransportGuard : Noncopyable, Nonmovable {
 public:
  TransportGuard(Transport<b, s, rpcs...> &t_) : t(t_) {
    // TODO add a futex to fast detect race and then die.
    poller = boost::fibers::fiber([this]() { t.progress_until([this] { return exit; }); });
    DEBUG("Transport attach to current thread");
  }
  ~TransportGuard() {
    exit = true;
    poller.join();
    DEBUG("Transport dettach with current thread");
  }

 private:
  bool exit = false;
  Transport<b, s, rpcs...> &t;
  boost::fibers::fiber poller;
};
