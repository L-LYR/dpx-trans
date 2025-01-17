#pragma once

namespace dpx {

#define static_unreachable static_assert(false, "Unreachable!")

// TODO: use std::unreachable instead
[[noreturn]] inline void unreachable() { __builtin_unreachable(); }

}  // namespace dpx
