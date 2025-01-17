#pragma once

#include <zpp_bits.h>

#include <functional>

#include "concepts/rpc.hxx"

namespace dpx::trans {

template <zpp::bits::string_literal Name, typename RequestType, typename ResponseType>
  requires std::default_initializable<RequestType> &&
           (std::is_void_v<ResponseType> || std::default_initializable<ResponseType>)
struct RpcBase {
  using Request = RequestType;
  using Response = ResponseType;
  using Handler = std::function<Response(Request& req)>;
  inline constexpr static std::string name = std::string(Name.begin(), Name.end());
  inline constexpr static rpc_id_t id = zpp::bits::id_v<zpp::bits::sha1<Name>(), sizeof(int)>;
  Response operator()(Request&) const {
    if constexpr (!std::is_void_v<Response>) {
      return Response{};
    }
  }  // dummy default handler
};

}  // namespace dpx::trans
