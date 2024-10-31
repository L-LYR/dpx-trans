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

  Status s;
};

template <Side side>
class ConnectionHandleBase : Noncopyable, Nonmovable {
 public:
  ~ConnectionHandleBase() = default;

 protected:
  ConnectionHandleBase() = default;
};

template <>
struct std::formatter<Side> : std::formatter<const char*> {
  template <typename Context>
  Context::iterator format(Side s, Context out) const {
    switch (s) {
      case Side::ServerSide:
        return std::formatter<const char*>::format("server", out);
      case Side::ClientSide:
        return std::formatter<const char*>::format("client", out);
    }
  }
};

template <>
struct std::formatter<Status> : std::formatter<const char*> {
  template <typename Context>
  Context::iterator format(Status s, Context out) const {
    switch (s) {
      case Status::Idle:
        return std::formatter<const char*>::format("Idle", out);
      case Status::Ready:
        return std::formatter<const char*>::format("Ready", out);
      case Status::Running:
        return std::formatter<const char*>::format("Running", out);
      case Status::Stopped:
        return std::formatter<const char*>::format("Stopped", out);
    }
  }
};
