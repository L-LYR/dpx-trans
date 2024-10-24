#pragma once

#include "concept/rpc.hxx"
#include "glaze/json/write.hpp"
#include "util/logger.hxx"

struct PayloadType {
  uint32_t id;
  std::string message;
};

struct EchoRpc : RpcBase<"Echo", PayloadType, PayloadType> {
  inline const static Handler handler = [](Request&& req) -> Response {
    INFO("{}", glz::write_json<>(req).value_or("Corrupted Payload!"));
    req.id++;
    req.message += ", World";
    return req;
  };
};

struct HelloRpc : RpcBase<"Hello", std::string, std::string> {
  inline const static Handler handler = [](Request&& req) -> Response {
    INFO("{}", req);
    return "Echo: " + req;
  };
};

static_assert(Rpc<EchoRpc>, "?");
static_assert(Rpc<HelloRpc>, "?");
