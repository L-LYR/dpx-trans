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
  using CtrlPathEndpoint =
    std::conditional_t<b == Backend::TCP,        tcp::Endpoint,
    std::conditional_t<b == Backend::Verbs,      verbs::Endpoint,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::Endpoint<s>,
                                                 void>>>;

  using CtrlPathConnHandle =
    std::conditional_t<b == Backend::TCP,        tcp::ConnectionHandle,
    std::conditional_t<b == Backend::Verbs,      verbs::ConnectionHandle,
    std::conditional_t<b == Backend::DOCA_Comch, doca::comch::ConnectionHandle<s>,
                                                 void>>>;

  using ConnectionParam = ConnectionParam<b>;
  using CtrlPathBuffers = naive::Buffers;
  using DataPathBuffers = std::conditional_t<b == Backend::DOCA_Comch, doca::Buffers, CtrlPathBuffers>;
  // clang-format on

  template <Backend, Side, Rpc...>
  friend class TransportGuard;

 public:
  Transport(Config<b> config_)
    requires(b == Backend::TCP || b == Backend::Verbs)
      : config(config_),
        cp_conn_handle(config.conn_param),
        cp_buffers(config.queue_depth, config.max_rpc_msg_size),
        dp_buffers(config.queue_depth, config.max_rpc_msg_size),
        cp_e(cp_buffers) {
    establish_connections();
  }

  Transport(doca::Device &dev, Config<b> config_)
    requires(b == Backend::DOCA_Comch)
      : config(config_),
        cp_conn_handle(config.conn_param),
        cp_buffers(config.queue_depth, config.max_rpc_msg_size),
        dp_buffers(dev, config.queue_depth, config.max_rpc_msg_size),
        cp_e(dev, cp_buffers, dp_buffers, config.conn_param.name) {
    establish_connections();
  }

  ~Transport() { terminate_connections(); }

  template <Rpc Rpc>
  resp_future_t<Rpc> call(const req_t<Rpc> &r)
    requires(s == Side::ClientSide)
  {
    auto call_seq = seq++;
    {
      TRACE("caller post recv");
      auto recv_ctx = new OpContext(Op::Recv, acquire_buffer());
      auto n_read_f = cp_e.post_recv(*recv_ctx);
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
    auto n_write_f = cp_e.post_send(send_ctx);
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
      TRACE("send seq: {}, id: {}", -seq, id);
      serializer(-seq, Rpc::id, resp).or_throw();
      return true;
    }
    return false;
  }

  void serve_once(size_t idx)
    requires(s == Side::ServerSide)
  {
    // constexpr auto n = sizeof...(Rpcs);
    // constexpr uint64_t ids[] = {Rpcs::id...};

    if (!cp_e.running()) {
      TRACE("not running");
      return;
    }

    auto buf = acquire_buffer();
    buf.clear();

    TRACE("worker {} post recv", idx);
    OpContext recv_ctx(Op::Recv, std::move(buf));
    auto n_read = cp_e.post_recv(recv_ctx).get();
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
    auto n_write = cp_e.post_send(send_ctx).get();
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
      if (auto buf = cp_buffers.acquire_one(); buf.has_value()) {
        return std::move(buf).value();
      } else {
        boost::this_fiber::yield();
      }
    }
  }

  void release_buffer(BorrowedBuffer &&buf) { cp_buffers.release_one(std::move(buf)); }

  void progress_until(std::function<bool()> &&predictor) {
    while (!(outstanding_rpcs.empty() && active_workers == 0 && predictor())) {
      if (!cp_e.progress()) {
        boost::this_fiber::yield();
      }
    }
  }

  const Config<b> config;
  int64_t seq = 1;
  CtrlPathConnHandle cp_conn_handle;
  CtrlPathBuffers cp_buffers;
  DataPathBuffers dp_buffers;
  CtrlPathEndpoint cp_e;
  std::unordered_map<int64_t, ContextBase *> outstanding_rpcs;
  uint32_t active_workers = 0;
};

template <Backend b, Side s, Rpc... rpcs>
class TransportGuard : Noncopyable, Nonmovable {
 public:
  TransportGuard(Transport<b, s, rpcs...> &t_) : t(t_) {
    // TODO add a futex to fast detect race and then die.
    poller = boost::fibers::fiber([this]() { t.progress_until([this] { return exit; }); });
    TRACE("Transport attach to current thread");
  }
  ~TransportGuard() {
    exit = true;
    poller.join();
    TRACE("Transport dettach with current thread");
  }

 private:
  bool exit = false;
  Transport<b, s, rpcs...> &t;
  boost::fibers::fiber poller;
};
