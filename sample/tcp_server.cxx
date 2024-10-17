#include "tcp_common.hxx"

int main() {
  Endpoint e1(Buffers(10));
  Endpoint e2(Buffers(10));
  std::jthread bg_connector([&]() { Connector("192.168.200.20", 10086).listen_loop({e1, e2}); });

  auto echo = [](Endpoint& e) {
    e.wait_and_then([&](int sock) {
      if (sock < 0) {
        die("Fail to establish connection, errno {}", sock);
      }
      e.set_sock(sock);
    });
    auto payload = e.read<PayloadType>();
    SPDLOG_INFO("{} {}", payload.id, payload.message);
    payload.id++;
    payload.message += ", World";
    e.write(std::move(payload));
  };

  std::jthread bg_e1(echo, std::ref(e1));
  std::jthread bg_e2(echo, std::ref(e2));
  return 0;
}
