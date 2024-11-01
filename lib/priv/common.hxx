#pragma once

#include <boost/fiber/future.hpp>
#include <cassert>
#include <format>

#include "concept/rpc.hxx"
#include "util/logger.hxx"
#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

enum class Op {
  Send,
  Recv,
};

struct ContextBase {};

using op_res_promise_t = boost::fibers::promise<int>;
using op_res_future_t = boost::fibers::future<int>;

struct OpContext : public ContextBase {
  op_res_promise_t op_res = {};
};

template <Rpc Rpc>
using resp_promise_t = boost::fibers::promise<resp_t<Rpc>>;
template <Rpc Rpc>
using resp_future_t = boost::fibers::future<resp_t<Rpc>>;

template <Rpc Rpc>
struct RpcContext : public ContextBase {
  resp_promise_t<Rpc> resp = {};
};

enum class Backend {
  TCP,
  Verbs,
  DOCA_RDMA,
  DOCA_DMA,
  DOCA_Comch,
  DOCA_DPA_MsgQ,
  DOCA_DPA_RDMA,
};

enum class Side {
  ClientSide,
  ServerSide,
};

// TODO: unused currently
enum class Status {
  Idle,
  Ready,
  Running,
  Stopped,
};

class EndpointBase : Noncopyable, Nonmovable {
 public:
  explicit EndpointBase(Status s_ = Status::Idle) : s(s_) {}
  ~EndpointBase() = default;

  bool idle() const { return s == Status::Idle; }
  bool ready() const { return s == Status::Ready; }
  bool running() const { return s == Status::Running; }
  bool stopped() const { return s == Status::Stopped; }

 protected:
  void prepare() {
    assert(idle());
    s = Status::Ready;
    TRACE("Endpoint status change: Idle -> Ready");
  }
  void run() {
    assert(ready());
    s = Status::Running;
    TRACE("Endpoint status change: Ready -> Running");
  }
  void stop() {
    assert(running());
    s = Status::Stopped;
    TRACE("Endpoint status change: Running -> Stopped");
  }

  std::atomic<Status> s;
};

struct ConnectionParam {
  bool passive;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = 0;
  uint16_t local_port = 0;
};

template <typename Derived, typename Endpoint>
class ConnectionHandleBase : Noncopyable, Nonmovable {
 public:
  using EndpointRef = std::reference_wrapper<Endpoint>;
  using EndpointRefs = std::vector<EndpointRef>;

  ~ConnectionHandleBase() = default;

  Derived &associate(Endpoint &e) {
    pending_endpoints.emplace_back(e);
    return *static_cast<Derived *>(this);
  }
  Derived &associate(EndpointRefs &&es) {
    pending_endpoints.insert(pending_endpoints.end(), std::make_move_iterator(es.begin()),
                             std::make_move_iterator(es.end()));
    return *static_cast<Derived *>(this);
  }

 protected:
  ConnectionHandleBase(const ConnectionParam &param_) : param(param_) {}

  const ConnectionParam &param;
  EndpointRefs pending_endpoints;
};

template <>
struct std::formatter<Side> : std::formatter<const char *> {
  template <typename Context>
  Context::iterator format(Side s, Context out) const {
    switch (s) {
      case Side::ServerSide:
        return std::formatter<const char *>::format("server", out);
      case Side::ClientSide:
        return std::formatter<const char *>::format("client", out);
    }
  }
};

template <>
struct std::formatter<Status> : std::formatter<const char *> {
  template <typename Context>
  Context::iterator format(Status s, Context out) const {
    switch (s) {
      case Status::Idle:
        return std::formatter<const char *>::format("Idle", out);
      case Status::Ready:
        return std::formatter<const char *>::format("Ready", out);
      case Status::Running:
        return std::formatter<const char *>::format("Running", out);
      case Status::Stopped:
        return std::formatter<const char *>::format("Stopped", out);
    }
  }
};
