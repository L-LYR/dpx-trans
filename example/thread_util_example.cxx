#include <iostream>
#include <thread>

#include "util/thread_util.hxx"
#include "util/timer.hxx"

using namespace std::literals;

using namespace dpx::trans;

int main() {
  set_thread_name("main");

  Timer t;

  bind_core(7777);

  std::this_thread::sleep_for(1s);

  std::cout << t.elapsed_ns() << std::endl;

  std::cout << get_thread_name() << std::endl;

  return 0;
}
