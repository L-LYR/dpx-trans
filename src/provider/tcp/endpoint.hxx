#pragma once

#include <liburing.h>

#include "def.hxx"

namespace dpx::trans::tcp {

class ConnHolder;

class Endpoint {
  friend class ConnHolder;

 public:
  Endpoint() {}
  ~Endpoint() {}

  

 private:
  template <Op op>
  void post();

  Side s;
  io_uring ring;
  int conn = -1;
};

}  // namespace dpx::trans::tcp
