#include "tcp_common.hxx"

int main() {
  Connector("192.168.200.20", 10086, "192.168.200.20", 10087).do_connect();
  return 0;
}
