#pragma once

#include <doca_ctx.h>

#include <cstdint>
#include <format>

enum class Op : uint32_t {
  Send,
  Recv,
};

enum class Status : uint32_t {
  Idle,
  Ready,
  Running,
  Stopping,
  Exited,
};

enum class Side : uint32_t {
  ClientSide,
  ServerSide,
};

enum class Backend : uint32_t {
  TCP,
  Verbs,
  DOCA_Comch,
  DOCA_RDMA,
  DOCA_DMA,
};

template <typename T>
inline constexpr std::underlying_type_t<T> to_underlying(T t) {
  return static_cast<std::underlying_type_t<T>>(t);
}

#define EnumFormatter(enum_type, ...)                                                       \
  template <>                                                                               \
  struct std::formatter<enum_type> : std::formatter<const char *> {                         \
    static constexpr const char *enum_type##_strs[] = {__VA_ARGS__};                        \
    template <typename Context>                                                             \
    Context::iterator format(enum_type e, Context out) const {                              \
      return std::formatter<const char *>::format(enum_type##_strs[to_underlying(e)], out); \
    }                                                                                       \
  }
// clang-format off
EnumFormatter(Status,
    [to_underlying(Status::Idle)]     = "Idle",
    [to_underlying(Status::Ready)]    = "Ready",
    [to_underlying(Status::Running)]  = "Running",
    [to_underlying(Status::Stopping)] = "Stopping",
    [to_underlying(Status::Exited)]   = "Exited",
);
EnumFormatter(Op,
    [to_underlying(Op::Send)] = "send",
    [to_underlying(Op::Recv)] = "recv",
);
EnumFormatter(Side,
    [to_underlying(Side::ServerSide)] = "Server",
    [to_underlying(Side::ClientSide)] = "Client",
);
// clang-format on
