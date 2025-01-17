#include "util/hex_dump.hxx"
#include <cstring>
#include <iostream>

int main() {
  const char *p = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::cout << dpx::Hexdump(p, strlen(p)) << std::endl;
  return 0;
}