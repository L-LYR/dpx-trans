#pragma once

#include <cassert>
#include <format>
#include <string>

#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

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
  }
  void run() {
    assert(ready());
    s = Status::Running;
  }
  void stop() {
    assert(running());
    s = Status::Stopped;
  }

  Status s;
};

class ConnectionBase : Noncopyable, Nonmovable {
 public:
  ~ConnectionBase() = default;

 protected:
  ConnectionBase(Side side_) : side(side_) {}

  Side side;
  std::string remote_addr = "";
  std::string local_addr = "";
};

template <Side side>
class ConnectionHandleBase : Noncopyable, Nonmovable {
 public:
  ~ConnectionHandleBase() = default;

 protected:
  ConnectionHandleBase() = default;
};

#ifdef USE_TCP

#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"

using Endpoint = tcp::Endpoint;
using Acceptor = tcp::Acceptor;
using Connector = tcp::Connector;

#endif

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
        return std::formatter<const char*>::format("idle", out);
      case Status::Ready:
        return std::formatter<const char*>::format("ready", out);
      case Status::Running:
        return std::formatter<const char*>::format("running", out);
      case Status::Stopped:
        return std::formatter<const char*>::format("stopped", out);
    }
  }
};
