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
  using BufferTypeRef = std::reference_wrapper<BufferType>;
  BufferPool(Buffers &&bs_) : bs(std::move(bs_)) {
    TRACE("{}", bs.n_elements());
    for (auto i = 0uz; i < bs.n_elements(); ++i) {
      q.emplace_back(bs[i]);
    }
  }
  ~BufferPool() = default;

  std::optional<BufferTypeRef> acquire_one() {
    if (q.empty()) {
      return {};
    }
    auto &buf = q.front().get();
    TRACE("acquire {} {}", (void *)buf.data(), buf.size());
    q.pop_front();
    return std::make_optional(buf);
  }
  void release_one(BufferType &buffer) {
    assert((buffer.size() == bs.piece_size() && bs.data() <= buffer.data() &&
            buffer.data() + buffer.size() <= bs.data() + bs.size()));
    q.emplace_back(buffer);
    TRACE("release {} {}", (void *)q.back().get().data(), q.back().get().size());
  }
  Buffers &buffers() { return bs; }
  const Buffers &buffers() const { return bs; }

 private:
  using BufferQ = std::list<BufferTypeRef>;

  Buffers bs;
  BufferQ q;
};