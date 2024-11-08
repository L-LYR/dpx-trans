#pragma once

#include <boost/fiber/future.hpp>

#include "concept/rpc.hxx"
#include "priv/defs.hxx"

class BufferBase;

struct ContextBase {};

using op_res_promise_t = boost::fibers::promise<int>;
using op_res_future_t = boost::fibers::future<int>;

struct OpContext : public ContextBase {
  Op op;
  BufferBase &buf;
  size_t len = -1;
  op_res_promise_t op_res = {};

  OpContext(Op op_, BufferBase &buf_);
  OpContext(Op op_, BufferBase &buf_, size_t len_);
};

template <Rpc Rpc>
using resp_promise_t = boost::fibers::promise<resp_t<Rpc>>;
template <Rpc Rpc>
using resp_future_t = boost::fibers::future<resp_t<Rpc>>;

template <Rpc Rpc>
struct RpcContext : public ContextBase {
  resp_promise_t<Rpc> resp = {};
};
