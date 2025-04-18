#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <thread>

#include "provider/tcp/connector.hxx"
#include "provider/tcp/endpoint.hxx"

using namespace dpx::trans;
using namespace dpx::trans::tcp;
using namespace std::chrono_literals;

void server() {
  Connector<Side::ServerSide> c("192.168.200.21", 10087);
  asio::io_context io(1);
  auto es = c.accept(io, 1);
  auto test = [&]() -> asio::awaitable<void> {
    char msg[128] = "hello";
    MemoryRegion mr(msg, strlen(msg));
    auto n = co_await es[0].post<Op::Read>(mr);
    REQUIRE(n == 5);
    REQUIRE(std::string_view(msg, 5) == "world");
    std::cout << "server ok" << std::endl;
    co_return;
  };
  asio::co_spawn(io, test(), asio::detached);
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](auto, auto) { io.stop(); });
  io.run();
}

void client() {
  Connector<Side::ClientSide> c("192.168.200.21", 10087, "192.168.201.21", 10086);
  asio::io_context io(1);
  auto es = c.connect(io, 1);
  auto test = [&]() -> asio::awaitable<void> {
    char msg[128] = "world";
    MemoryRegion mr(msg, strlen(msg));
    auto n = co_await es[0].post<Op::Write>(mr);
    REQUIRE(n == 5);
    std::cout << "client ok" << std::endl;
    co_return;
  };
  asio::co_spawn(io, test(), asio::detached);
  asio::signal_set signals(io, SIGINT, SIGTERM);
  signals.async_wait([&](auto, auto) { io.stop(); });
  io.run();
}

TEST_CASE("TCP Connection Holder") {
  std::thread s(server);
  std::this_thread::sleep_for(2s);
  std::thread c(client);
  s.join();
  c.join();
}