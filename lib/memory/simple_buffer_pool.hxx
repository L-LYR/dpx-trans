#pragma once

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
  BufferPool(Buffers &&bs_) : bs(std::move(bs_)) {
    TRACE("{}", bs.n_elements());
    for (auto i = 0uz; i < bs.n_elements(); ++i) {
      q.emplace_back(bs[i]);
    }
  }
  ~BufferPool() = default;

  std::optional<std::reference_wrapper<BufferType>> acquire_one() {
    if (q.empty()) {
      return {};
    }
    auto &buf_ref = q.front();
    TRACE("{} {}", (void *)buf_ref.get().data(), buf_ref.get().size());
    q.pop_front();
    return std::make_optional(buf_ref);
  }
  void release_one(BufferType &buffer) {
    assert((buffer.size() == bs.piece_size() && bs.data() <= buffer.data() &&
            buffer.data() + buffer.size() <= bs.data() + bs.size()));
    q.push_back(buffer);
  }
  Buffers &buffers() { return bs; }
  const Buffers &buffers() const { return bs; }

 private:
  using BufferQ = std::list<std::reference_wrapper<BufferType>>;

  Buffers bs;
  BufferQ q;
};