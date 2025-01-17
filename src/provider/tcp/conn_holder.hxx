#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "def.hxx"

namespace dpx::trans::tcp {

class Endpoint;

struct ConnHolderConfig {
  Side s;
  std::string remote_ip;
  uint16_t remote_port;
  std::string local_ip;
  uint16_t local_port;
};

class ConnHolder {
 public:
  explicit ConnHolder(const ConnHolderConfig& config);
  ~ConnHolder();

  void associate(Endpoint& e);
  void establish();
  void terminate();

 private:
  ConnHolderConfig config;
  std::vector<Endpoint*> pending;
  std::unordered_map<int, Endpoint*> established;
  int ep_fd = -1;
  int listen_sock = -1;
};

}  // namespace dpx::trans::tcp
