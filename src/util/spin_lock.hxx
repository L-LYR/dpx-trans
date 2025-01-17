#pragma once

#include <atomic>

namespace dpx::trans {

class alignas(64) SpinLock {
 public:
  SpinLock() = default;
  ~SpinLock() = default;

  void lock();
  [[nodiscard]] bool try_lock();
  void unlock();

 private:
  std::atomic_bool b = false;
};

}  // namespace dpx::trans
