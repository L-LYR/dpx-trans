#pragma once

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
