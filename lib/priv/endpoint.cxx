#include "priv/endpoint.hxx"

#include "util/logger.hxx"

void EndpointBase::prepare() {
  assert(idle());
  s = Status::Ready;
  INFO("Endpoint status change: Idle -> Ready");
}

void EndpointBase::run() {
  assert(ready());
  s = Status::Running;
  INFO("Endpoint status change: Ready -> Running");
}

void EndpointBase::stop() {
  assert(running());
  s = Status::Stopping;
  INFO("Endpoint status change: Running -> Stopped");
}

void EndpointBase::shutdown() {
  assert(stopping());
  s = Status::Exited;
  INFO("Endpoint status change: Stopped -> Exited");
}