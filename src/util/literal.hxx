#pragma once

#include <cstdint>

namespace dpx::trans::literal {

constexpr auto operator""_KB(unsigned long long int x) -> uint64_t { return 1024ULL * x; }

constexpr auto operator""_MB(unsigned long long int x) -> uint64_t { return 1024_KB * x; }

constexpr auto operator""_GB(unsigned long long int x) -> uint64_t { return 1024_MB * x; }

constexpr auto operator""_TB(unsigned long long int x) -> uint64_t { return 1024_GB * x; }

constexpr auto operator""_PB(unsigned long long int x) -> uint64_t { return 1024_TB * x; }

}  // namespace dpx::trans::literal

// How to use:
//  Add the following line `using namespace dpx::trans::literal;`
