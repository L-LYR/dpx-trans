#pragma once

#include <cassert>
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
  void stop() { s = Status::Stopped; }

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

class ConnectionHandleBase : Noncopyable, Nonmovable {
 public:
  ~ConnectionHandleBase() = default;

 protected:
  ConnectionHandleBase(Side side_) : side(side_) {}

  Side side;
};

#ifdef USE_TCP

#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"

using Endpoint = tcp::Endpoint;
using Acceptor = tcp::Acceptor;
using Connector = tcp::Connector;

#endif