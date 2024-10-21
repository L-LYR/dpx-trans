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

  void run() { s = Status::Running; }
  void stop() { s = Status::Stopped; }

 protected:
  Status s;  // TODO: maybe check in another thread
};

class ConnectionBase : Noncopyable, Nonmovable {
 public:
  ~ConnectionBase() {}

  std::pair<std::string, uint16_t> remote() const { return {remote_ip, remote_port}; }
  std::pair<std::string, uint16_t> local() const { return {local_ip, local_port}; }

 protected:
  ConnectionBase(Side side_, std::string remote_ip_, uint16_t remote_port_, std::string local_ip_, uint16_t local_port_)
      : side(side_), remote_ip(remote_ip_), local_ip(local_ip_), remote_port(remote_port_), local_port(local_port_) {}

  Side side;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = -1;
  uint16_t local_port = -1;
};
