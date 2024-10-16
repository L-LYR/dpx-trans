#pragma once

class Nonmovable {
 protected:
  Nonmovable() noexcept = default;
  ~Nonmovable() noexcept = default;

 public:
  Nonmovable(Nonmovable&&) = delete;
  Nonmovable& operator=(Nonmovable&&) = delete;
};
