#pragma once

#include <concepts>
#include <cstdint>

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
