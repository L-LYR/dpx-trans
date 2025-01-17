#pragma once

namespace dpx {

class Nonmovable {
 protected:
  Nonmovable() noexcept = default;
  ~Nonmovable() noexcept = default;

 public:
  Nonmovable(Nonmovable&&) = delete;
  Nonmovable& operator=(Nonmovable&&) = delete;
};

}  // namespace dpx
