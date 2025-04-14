#pragma once

#include <cstdint>
#include <format>

#include "util/enum_formatter.hxx"

namespace dpx::trans {

enum class Backend : uint32_t {
  TCP,
  Verbs,
  DOCA_Comch,
  DOCA_RDMA,
};

enum class Side : uint32_t {
  ClientSide,
  ServerSide,
};

enum class Op : uint32_t {
  Send,
  Recv,
  Read,
  Write,
};

enum class ErrorCode : ssize_t {
  Nop = -10086,
  EndpointIsStopping,
};

}  // namespace dpx::trans

EnumFormatter(4, dpx::trans::Backend, TCP, Verbs, DOCA_Comch, DOCA_RDMA);
EnumFormatter(2, dpx::trans::Side, ServerSide, ClientSide);
EnumFormatter(4, dpx::trans::Op, Send, Recv, Read, Write);
