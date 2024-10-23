#pragma once

#include <zpp_bits.h>

#include <functional>

#include "concept/rpc.hxx"

using namespace zpp::bits::literals;
struct PayloadType {
  uint32_t id;
  std::string message;
};

struct EchoRpc {
  inline constexpr static uint64_t id = "Echo"_sha1_int;
  using Request = PayloadType;
  using Response = PayloadType;
  using Handler = const std::function<Response(Request)>;
  static Handler handler;
};

static_assert(Rpc<EchoRpc>, "?");
