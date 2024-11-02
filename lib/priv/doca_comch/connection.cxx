#include "priv/doca_comch/connection.hxx"

#include "priv/doca_comch/endpoint.hxx"

using namespace std::chrono_literals;

namespace doca::comch::ctrl_path {

ConnectionHandle::ConnectionHandle(const ConnectionParam& param_) : ConnectionHandleBase(param_) {}

ConnectionHandle::~ConnectionHandle() {}

void ConnectionHandle::progress_all_until(std::function<bool(Endpoint& e)>&& fn) {
  while (true) {
    uint32_t n_satisfied = 0;
    for (Endpoint& e : pending_endpoints) {
      if (fn(e)) {
        e.progress();
      } else {
        n_satisfied++;
      }
    }
    if (n_satisfied == pending_endpoints.size()) {
      return;
    } else {
      std::this_thread::sleep_for(10us);
    }
  }
}

void ConnectionHandle::listen_and_accept() {
  progress_all_until([](Endpoint& e) { return e.conn != nullptr; });
}

void ConnectionHandle::wait_for_disconnect() {
  progress_all_until([](Endpoint& e) { return e.conn == nullptr; });
}

void ConnectionHandle::connect() {
  progress_all_until([](Endpoint& e) { return e.conn != nullptr; });
}

void ConnectionHandle::disconnect() {
  std::ranges::for_each(pending_endpoints, [](Endpoint& e) { e.stop(); });
  progress_all_until([](Endpoint& e) { return e.conn == nullptr; });
}

}  // namespace doca::comch::ctrl_path