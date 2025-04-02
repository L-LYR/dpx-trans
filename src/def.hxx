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

}  // namespace dpx::trans

// clang-format off
EnumFormatter(dpx::trans::Backend,
    [dpx::trans::to_underlying(dpx::trans::Backend::TCP)]        = "TCP",
    [dpx::trans::to_underlying(dpx::trans::Backend::Verbs)]      = "Verbs",
    [dpx::trans::to_underlying(dpx::trans::Backend::DOCA_Comch)] = "DOCA Comch",
    [dpx::trans::to_underlying(dpx::trans::Backend::DOCA_RDMA)]  = "DOCA RDMA",
);

EnumFormatter(dpx::trans::Side,
    [dpx::trans::to_underlying(dpx::trans::Side::ServerSide)] = "Server",
    [dpx::trans::to_underlying(dpx::trans::Side::ClientSide)] = "Client",
);

EnumFormatter(dpx::trans::Op,
    [dpx::trans::to_underlying(dpx::trans::Op::Send)]  = "Send",
    [dpx::trans::to_underlying(dpx::trans::Op::Recv)]  = "Recv",
    [dpx::trans::to_underlying(dpx::trans::Op::Read)]  = "Read",
    [dpx::trans::to_underlying(dpx::trans::Op::Write)] = "Write",
);
// clang-format on
