#pragma once

class Noncopyable {
 protected:
  Noncopyable() noexcept = default;
  ~Noncopyable() noexcept = default;

 public:
  Noncopyable(const Noncopyable &) = delete;
  Noncopyable &operator=(const Noncopyable &) = delete;
};