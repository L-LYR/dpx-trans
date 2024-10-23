#include "echo.hxx"

#include <glaze/glaze.hpp>

#include "util/logger.hxx"

handler_t<EchoRpc> EchoRpc::handler = [](PayloadType&& req) -> PayloadType {
  INFO("{}", glz::write_json<>(req).value_or("Corrupted Payload!"));
  req.id++;
  req.message += ", World";
  return req;
};
