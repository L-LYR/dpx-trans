#include <iostream>
#include <thread>

#include "util/thread_util.hxx"
#include "util/timer.hxx"

using namespace std::literals;

int main() {
  dpx::set_thread_name("main");

  dpx::Timer t;

  dpx::bind_core(7777);

  std::this_thread::sleep_for(1s);

  std::cout << t.elapsed_ns() << std::endl;

  std::cout << dpx::get_thread_name() << std::endl;

  return 0;
}
