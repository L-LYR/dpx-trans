#include <iostream>
#include <string>

#include "util/doca_wrapper.hxx"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return -2;
  }
  std::string addr = argv[1];
  try {
    auto dev = doca_wrapper::open_doca_dev(addr);
    auto rep = doca_wrapper::open_doca_dev_rep(dev, "");
  } catch (std::runtime_error& e) {
    std::cerr << e.what() << std::endl;
    return -1;
  }
  return 0;
}