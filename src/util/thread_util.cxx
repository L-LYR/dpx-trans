#include "util/thread_util.hxx"

#include <pthread.h>

#include <thread>

#include "util/fatal.hxx"
#include "util/logger.hxx"

namespace dpx::trans {

void set_thread_name(std::string name) {
  if (name.size() > 16) {
    LOG_CRITI("\"{}\" is too long. Name length is restricted to 16 characters.", name);
    return;
  }
  if (auto ec = pthread_setname_np(pthread_self(), name.c_str()); ec != 0) {
    die("Fail to set thread name `{}`, errno: {}", name, errno);
  }
}

std::string get_thread_name() {
  std::string name(16, ' ');
  if (auto ec = pthread_getname_np(pthread_self(), name.data(), name.size()); ec != 0) {
    die("Fail to get thread name, errno: {}", name, errno);
  }
  return name;
}

void bind_core(size_t core_idx) {
  if (core_idx > std::thread::hardware_concurrency()) {
    LOG_CRITI("{} is a wrong core index, hardware concurrency is {}", core_idx, std::thread::hardware_concurrency());
    return;
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_idx, &cpuset);
  if (auto ec = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset); ec != 0) {
    die("Fail to set affinity for thread {}, errno: {}", errno);
  }
}

}  // namespace dpx::trans
