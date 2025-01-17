#pragma once

#include <chrono>

namespace dpx::trans {

class Timer {
  using clock = std::chrono::high_resolution_clock;
  using time_point = std::chrono::time_point<clock>;
  using ns = std::chrono::nanoseconds;
  using us = std::chrono::microseconds;
  using ms = std::chrono::milliseconds;
  using s = std::chrono::seconds;

 public:
  Timer() : b(clock::now()) {}

  void reset() { b = clock::now(); }

  ns elapsed_ns() { return elapsed<ns>(); }
  us elapsed_us() { return elapsed<us>(); }
  ms elapsed_ms() { return elapsed<ms>(); }
  s elapsed_s() { return elapsed<s>(); }

 private:
  template <typename T>
  T elapsed() {
    return std::chrono::duration_cast<T>(clock::now() - b);
  }

  time_point b;
};

}  // namespace dpx::trans
