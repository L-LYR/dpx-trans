#include <cstring>
#include <iostream>

#include "util/hex_dump.hxx"

using namespace dpx::trans;

int main() {
  const char *p = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::cout << Hexdump(p, strlen(p)) << std::endl;
  return 0;
}