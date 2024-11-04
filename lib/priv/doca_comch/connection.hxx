#pragma once

#include "priv/common.hxx"

using namespace std::chrono_literals;

namespace doca::comch {

struct ConnectionParam {
  std::string name;
};

template <Side s>
class Endpoint;

template <Side s>
class ConnectionHandle {
  using Endpoint = Endpoint<s>;
  using EndpointRef = std::reference_wrapper<Endpoint>;
  using EndpointRefs = std::vector<EndpointRef>;
  using Predictor = std::function<bool(Endpoint&)>;

 public:
  ConnectionHandle(const ConnectionParam& param_) : param(param_) {}

  ~ConnectionHandle() {}

  ConnectionHandle& associate(Endpoint& e) {
    pending_endpoints.emplace_back(e);
    return *this;
  }

  ConnectionHandle& associate(EndpointRefs&& es) {
    pending_endpoints.insert(pending_endpoints.end(), std::make_move_iterator(es.begin()),
                             std::make_move_iterator(es.end()));
    return *this;
  }

  void listen_and_accept() {
    progress_all_until([](Endpoint& e) { return e.conn != nullptr; });
    for_each_endpoint([](Endpoint& e) { e.prepare(); });
    progress_all_until([](Endpoint& e) { return e.running(); });
  }

  void wait_for_disconnect() {
    progress_all_until([](Endpoint& e) { return e.exited(); });
  }

  void connect() {
    progress_all_until([](Endpoint& e) { return e.conn != nullptr; });
    for_each_endpoint([](Endpoint& e) { e.prepare(); });
    progress_all_until([](Endpoint& e) { return e.running(); });
  }

  void disconnect() {
    for_each_endpoint([](Endpoint& e) { e.stop(); });
    progress_all_until([](Endpoint& e) { return e.exited(); });
  }

 private:
  template <typename Fn>
  void for_each_endpoint(Fn&& fn) {
    std::ranges::for_each(pending_endpoints, fn);
  }
  void progress_all_until(Predictor&& p) {
    while (true) {
      uint32_t n_satisfied = 0;
      for_each_endpoint([&n_satisfied, p = std::move(p)](Endpoint& e) {
        if (!p(e)) {
          e.progress();
        } else {
          n_satisfied++;
        }
      });
      if (n_satisfied == pending_endpoints.size()) {
        return;
      } else {
        std::this_thread::sleep_for(10us);
      }
    }
  }

  const ConnectionParam& param;
  EndpointRefs pending_endpoints;
};

}  // namespace doca::comch
