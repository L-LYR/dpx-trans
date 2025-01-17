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
  Side s;
  int conn = -1;
};

}  // namespace dpx::trans::tcp
