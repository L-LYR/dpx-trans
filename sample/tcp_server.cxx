#include "tcp_common.hxx"

int main() {
  Connector("192.168.200.20", 10086).do_listen([](int sock) {
    Buffers buffers(10);
    Endpoint e(sock, std::move(buffers));
    auto payload = e.post_read<PayloadType>();
    std::cout << payload.id << " " << payload.message << std::endl;
    payload.id++;
    payload.message += ", World";
    e.post_write(std::move(payload));
  });
  return 0;
}
