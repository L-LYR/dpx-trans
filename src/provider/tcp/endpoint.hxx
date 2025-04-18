#pragma once

#include <asio.hpp>

#include "def.hxx"
#include "memory_region.hxx"
#include "util/logger.hxx"
#include "util/noncopyable.hxx"
#include "util/unreachable.hxx"

namespace dpx::trans::tcp {

class Endpoint : Noncopyable {
 public:
  explicit Endpoint(asio::ip::tcp::socket conn_) : conn(std::move(conn_)) {}

  ~Endpoint() { conn.close(); };

  Endpoint(Endpoint&& other) : conn(std::move(other.conn)) {}
  Endpoint& operator=(Endpoint&& other) {
    if (this != &other) {
      conn = std::move(other.conn);
    }
    return *this;
  }

  template <Op op>
  asio::awaitable<size_t> post(MemoryRegion& mr) {
    LOG_DEBUG("tcp post {} {}", op, mr);
    if constexpr (op == Op::Send || op == Op::Write) {
      co_return co_await asio::async_write(conn, asio::const_buffer(mr.raw_data(), mr.size()), asio::use_awaitable);
    } else if constexpr (op == Op::Recv || op == Op::Read) {
      co_return co_await asio::async_read(conn, asio::mutable_buffer(mr.raw_data(), mr.size()), asio::use_awaitable);
    } else {
      static_unreachable;
    }
  }

 private:
  asio::ip::tcp::socket conn;
};

}  // namespace dpx::trans::tcp
