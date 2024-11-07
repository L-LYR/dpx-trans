#pragma once
#include <doca_ctx.h>

#include <format>
#include <string>

#include "priv/common.hxx"

namespace {
template <typename T>
inline constexpr std::underlying_type_t<T> to_underlying(T t)
  requires(std::is_convertible_v<std::underlying_type_t<T>, size_t>)
{
  return static_cast<std::underlying_type_t<T>>(t);
}
// clang-format off
constexpr const char *Op_strs[] = {
    [to_underlying(Op::Send)] = "send",
    [to_underlying(Op::Recv)] = "recv",
};
constexpr const char *Side_strs[] = {
    [to_underlying(Side::ServerSide)] = "Server",
    [to_underlying(Side::ClientSide)] = "Client",
};
constexpr const char *Status_strs[] = {
    [to_underlying(Status::Idle)] = "Idle",
    [to_underlying(Status::Ready)] = "Ready",
    [to_underlying(Status::Running)] = "Running",
    [to_underlying(Status::Stopping)] = "Stopping",
    [to_underlying(Status::Exited)] = "Exited",
};
constexpr const char *doca_ctx_states_strs[] = {
    [DOCA_CTX_STATE_IDLE] = "Idle",
    [DOCA_CTX_STATE_STARTING] = "Starting",
    [DOCA_CTX_STATE_RUNNING] = "Running",
    [DOCA_CTX_STATE_STOPPING] = "Stopping",
};
// clang-format on
}  // namespace

#define EnumFormatter(enum_type)                                                            \
  template <>                                                                               \
  struct std::formatter<enum_type> : std::formatter<const char *> {                         \
    template <typename Context>                                                             \
    Context::iterator format(enum_type e, Context out) const {                              \
      return std::formatter<const char *>::format(enum_type##_strs[to_underlying(e)], out); \
    }                                                                                       \
  }

EnumFormatter(Status);
EnumFormatter(Op);
EnumFormatter(Side);
EnumFormatter(doca_ctx_states);

template <>
struct std::formatter<OpContext> : std::formatter<std::string> {
  template <typename Context>
  Context::iterator format(const OpContext &ctx, Context out) const {
    return std::formatter<std::string>(
        std::format("{} op, buf: {}, buf_len: {}, data_len: {}", ctx.op, reinterpret_cast<const void *>(ctx.buf.data()),
                    ctx.buf.size(), ctx.len),
        out);
  }
};
