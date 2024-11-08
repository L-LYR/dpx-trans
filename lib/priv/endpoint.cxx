#include "priv/endpoint.hxx"

#include "util/logger.hxx"

void EndpointBase::prepare() {
  assert(idle());
  s = Status::Ready;
  DEBUG("Endpoint status change: Idle -> Ready");
}

void EndpointBase::run() {
  assert(ready());
  s = Status::Running;
  DEBUG("Endpoint status change: Ready -> Running");
}

void EndpointBase::stop() {
  assert(running());
  s = Status::Stopping;
  DEBUG("Endpoint status change: Running -> Stopped");
}

void EndpointBase::shutdown() {
  assert(stopping());
  s = Status::Exited;
  DEBUG("Endpoint status change: Stopped -> Exited");
}