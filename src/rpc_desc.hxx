#pragma once

#include <functional>

#include "concepts/rpc.hxx"
#include "serializer/zpp_bits_serializer.hxx"
#include "util/constexpr_string.hxx"
#include "util/crc.hxx"

namespace dpx::trans {

template <c_string Name, typename RequestType, typename ResponseType, typename SerializerType = ZppBitsSerializer,
          typename DeserializerType = ZppBitsDeserializer>
  requires std::default_initializable<RequestType> &&
           (std::is_void_v<ResponseType> || std::default_initializable<ResponseType>)
struct RpcDesc {
  using Request = RequestType;
  using Response = ResponseType;
  using Handler = std::function<Response(Request& req)>;
  using Serializer = SerializerType;
  using Deserializer = DeserializerType;
  inline constexpr static c_string name = Name;
  inline constexpr static rpc_id_t id = crc::CRC64()(Name);
  Response operator()(Request&) const { throw std::runtime_error("Default handler is triggered!"); }
};

}  // namespace dpx::trans
