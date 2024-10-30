#pragma once

#include <chrono>

class Timer {
  using c = std::chrono::high_resolution_clock;
  using p = std::chrono::time_point<c>;
  using ns = std::chrono::nanoseconds;
  using us = std::chrono::microseconds;
  using ms = std::chrono::milliseconds;
  using s = std::chrono::seconds;

 public:
  Timer() : b(c::now()) {}

  void reset() { b = c::now(); }

  uint64_t elapsed_ns() { return elapsed<ns>(); }
  uint64_t elapsed_us() { return elapsed<us>(); }
  uint64_t elapsed_ms() { return elapsed<ms>(); }
  uint64_t elapsed_s() { return elapsed<s>(); }

 private:
  template <typename T>
  uint64_t elapsed() {
    return std::chrono::duration_cast<T>(c::now() - b).count();
  }

  p b{};
};
