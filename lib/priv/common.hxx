#pragma once

#include <cstdint>
#include <string>

#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

enum class Side {
  ClientSide,
  ServerSide,
};

// TODO: unused currently
enum class Status {
  Ready,
  Running,
  Stopped,
};

class EndpointBase : Noncopyable, Nonmovable {
 public:
  explicit EndpointBase(Status s_ = Status::Ready) : s(s_) {}
  ~EndpointBase() = default;

  bool ready() const { return s == Status::Ready; }
  bool running() const { return s == Status::Running; }
  bool stopped() const { return s == Status::Stopped; }

 protected:
  void run() { s = Status::Running; }
  void stop() { s = Status::Stopped; }

  Status s;
};

class ConnectionBase : Noncopyable, Nonmovable {
 public:
  ~ConnectionBase() = default;

 protected:
  ConnectionBase(Side side_) : side(side_) {}

  Side side;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = -1;
  uint16_t local_port = -1;
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
