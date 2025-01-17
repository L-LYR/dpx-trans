#pragma once

#include <concepts>
#include <cstdint>

namespace dpx::trans {

using rpc_id_t = uint64_t;

using rpc_seq_t = uint32_t;

template <typename T>
concept Rpc = requires(T rpc) {
  typename T::Request;
  typename T::Response;
  typename T::Handler;
  { rpc.id } -> std::same_as<rpc_id_t>;
};

template <Rpc Rpc>
using req_t = typename Rpc::Request;

template <Rpc Rpc>
using resp_t = typename Rpc::Response;

template <Rpc Rpc>
using handler_t = typename Rpc::Handler;

template <typename T, Rpc rpc>
struct is_handler_of : std::is_same<T, typename rpc::Handler> {};

template <typename T, Rpc rpc>
inline constexpr bool is_handler_of_v = is_handler_of<T, rpc>::value;

template <Rpc Rpc>
struct is_oneway : std::is_void<resp_t<Rpc>> {};

template <Rpc Rpc>
inline constexpr bool is_oneway_v = is_oneway<Rpc>::value;

}  // namespace dpx::trans
