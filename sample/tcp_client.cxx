#include "tcp_common.hxx"

int main() {
  Connector("192.168.200.20", 10086, "192.168.200.20", 10087).do_connect([](int sock) {
    Buffers buffers(10);
    Endpoint e(sock, std::move(buffers));
    e.post_write(PayloadType{
        .id = 1,
        .message = "Hello",
    });

    auto payload = e.post_read<PayloadType>();
    std::cout << payload.id << " " << payload.message << std::endl;
  });
  return 0;
}
