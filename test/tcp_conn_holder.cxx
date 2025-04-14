#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <thread>

#include "provider/tcp/conn_holder.hxx"
#include "provider/tcp/endpoint.hxx"

using namespace dpx::trans;
using namespace dpx::trans::tcp;
using namespace std::chrono_literals;

void server() {
  ConnHolder ch("192.168.200.21", 10087);
  Endpoint e(10);
  ch.associate(e);
  ch.establish();
  char msg[128] = "hello";
  MemoryRegion mr(msg, strlen(msg));
  Context ctx(Op::Recv, mr);
  auto f = e.post(ctx);
  while (!e.progress()) {
  }
  auto n = f.get();
  REQUIRE(n == 5);
  REQUIRE(std::string_view(msg, 5) == "world");
  std::this_thread::sleep_for(2s);
  ch.terminate();
}

void client() {
  ConnHolder ch("192.168.200.21", 10087, "192.168.201.21", 10086);
  Endpoint e(10);
  ch.associate(e);
  ch.establish();
  char msg[128] = "world";
  MemoryRegion mr(msg, strlen(msg));
  Context ctx(Op::Send, mr);
  auto f = e.post(ctx);
  while (!e.progress()) {
  }
  auto n = f.get();
  REQUIRE(n == 5);
  std::this_thread::sleep_for(2s);
  ch.terminate();
}

TEST_CASE("TCP Connection Holder") {
  std::thread s(server);
  std::this_thread::sleep_for(2s);
  std::thread c(client);
  s.join();
  c.join();
}