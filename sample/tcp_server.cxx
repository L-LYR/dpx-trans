#include "tcp_common.hxx"

int main() {
  Connector("192.168.200.20", 10086).do_listen();
  return 0;
}
