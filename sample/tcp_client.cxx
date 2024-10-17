#include "tcp_common.hxx"

int main() {
  Endpoint e(Buffers(10));
  Connector("192.168.200.20", 10086, "192.168.200.20", 10087).connect_with(e);
  e.write(PayloadType{
      .id = 1,
      .message = "Hello",
  });
  auto payload = e.read<PayloadType>();
  SPDLOG_INFO("{} {}", payload.id, payload.message);
  return 0;
}
