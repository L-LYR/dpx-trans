#pragma once

#include <functional>
#include <list>
#include <optional>

#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

template <typename T>
concept BuffersType = requires(T bs) {
  typename T::BufferType;
  { bs.n_elements() };
  { bs.piece_size() };
  { bs.data() };
  { bs.size() };
  { bs[0] };
};

template <BuffersType Buffers>
class BufferPool : Noncopyable, Nonmovable {
 public:
  using BufferType = typename Buffers::BufferType;
  using BufferTypeRef = std::reference_wrapper<BufferType>;
  template <typename... Args>
  BufferPool(Args &&...args) : bs(args...) {
    for (auto i = 0uz; i < bs.n_elements(); ++i) {
      q.emplace_back(std::ref(bs[i]));
    }
  }
  ~BufferPool() = default;

  std::optional<BufferTypeRef> acquire_one() {
    if (q.empty()) {
      return {};
    }
    auto buf = q.front();
    q.pop_front();
    return std::make_optional(buf);
  }
  void release_one(BufferType &buffer) {
    assert((buffer.size() == bs.piece_size() && bs.data() <= buffer.data() &&
            buffer.data() + buffer.size() <= bs.data() + bs.size()));
    buffer.clear();
    q.emplace_back(buffer);
  }
  Buffers &buffers() { return bs; }
  const Buffers &buffers() const { return bs; }

 private:
  using BufferQ = std::list<BufferTypeRef>;

  BufferQ q;
  Buffers bs;
};
