#include <iostream>
#include <thread>

#include "provider/tcp/conn_holder.hxx"
#include "provider/tcp/endpoint.hxx"
#include "util/logger.hxx"

using namespace dpx::trans;
using namespace std::literals;

int main(int argc, char* argv[]) try {
  tcp::ConnHolderConfig c = {
      .s = (argv[5] == std::string("client") ? Side::ClientSide : Side::ServerSide),
      .remote_ip = argv[1],
      .remote_port = (uint16_t)atoi(argv[2]),
      .local_ip = argv[3],
      .local_port = (uint16_t)atoi(argv[4]),
  };

  tcp::ConnHolder ch(c);
  tcp::Endpoint e1;
  tcp::Endpoint e2;
  ch.associate(e1);
  ch.associate(e2);

  ch.establish();

  for (uint32_t i = 0; i < 5; i++) {
    INFO("wait {}", i);
    std::this_thread::sleep_for(2s);
  }

  ch.terminate();

  return 0;
} catch (const std::runtime_error& e) {
  std::cout << e.what() << std::endl;
  return -1;
}
