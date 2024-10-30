#pragma once

#include <zpp_bits.h>

#include <concepts>
#include <cstdint>

template <typename Rpc>
using req_t = typename Rpc::Request;

template <typename Rpc>
using resp_t = typename Rpc::Response;

using rpc_id_t = uint64_t;

template <zpp::bits::string_literal name, typename RequestType, typename ResponseType>
  requires std::default_initializable<RequestType> && std::default_initializable<ResponseType>
struct RpcBase {
  using Request = RequestType;
  using Response = ResponseType;
  inline constexpr static rpc_id_t id = zpp::bits::id_v<zpp::bits::sha1<name>(), sizeof(int)>;
};

template <typename T>
concept Rpc = requires(T rpc, req_t<T> req, resp_t<T> resp) {
  { rpc.id } -> std::convertible_to<rpc_id_t>;
  { resp = rpc.operator()(req) };
};
