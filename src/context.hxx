#pragma once

#include <boost/fiber/future.hpp>

#include "def.hxx"
#include "memory_region.hxx"

namespace dpx::trans {

using op_res_promise_t = boost::fibers::promise<ssize_t>;
using op_res_future_t = boost::fibers::future<ssize_t>;

struct Context {
  Op op;
  op_res_promise_t op_res_p;
  MemoryRegion mr;
  size_t tx_size;

  explicit Context(Op op_, MemoryRegion mr_) : op(op_), mr(mr_) {}
};

}  // namespace dpx::trans
