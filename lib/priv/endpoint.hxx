#pragma once

#include <atomic>

#include "priv/defs.hxx"
#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

class EndpointBase : Noncopyable, Nonmovable {
 public:
  explicit EndpointBase(Status s_ = Status::Idle) : s(s_) {}
  ~EndpointBase() = default;

  bool idle() const { return s == Status::Idle; }
  bool ready() const { return s == Status::Ready; }
  bool running() const { return s == Status::Running; }
  bool stopping() const { return s == Status::Stopping; }
  bool exited() const { return s == Status::Exited; }

 protected:
  void prepare();
  void run();
  void stop();
  void shutdown();

  std::atomic<Status> s;
};
