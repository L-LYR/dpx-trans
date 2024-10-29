#pragma once

#define static_unreachable static_assert(false, "Unreachable!");

[[noreturn]] inline void unreachable() { __builtin_unreachable(); }
