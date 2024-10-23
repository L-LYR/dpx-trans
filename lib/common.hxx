#pragma once

#include <zpp_bits.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

 protected:
  ConnectionBase(Side side_) : side(side_) {}

  Side side;
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = -1;
  uint16_t local_port = -1;
};

class Connection;
using ConnectionPtr = std::unique_ptr<Connection>;

class Endpoint;
using EndpointRef = std::reference_wrapper<Endpoint>;
using EndpointRefs = std::vector<EndpointRef>;

template <typename Rpc>
using req_t = typename Rpc::Request;

template <typename Rpc>
using resp_t = typename Rpc::Response;

template <typename Rpc>
using handler_t = typename Rpc::Handler;

template <typename T>
concept Rpc = requires(T rpc, req_t<T> req, resp_t<T> resp) {
  { rpc.id } -> std::convertible_to<uint64_t>;
  { resp = rpc.handler(req) };
};
