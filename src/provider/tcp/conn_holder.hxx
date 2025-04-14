#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "def.hxx"

namespace dpx::trans::tcp {

class Endpoint;

struct ConnHolderConfig {
  std::string remote_ip;
  std::string local_ip;
  uint16_t remote_port;
  uint16_t local_port;
  Side side;
};

class ConnHolder {
 public:
  explicit ConnHolder(const ConnHolderConfig& config);

  ConnHolder(std::string remote_ip, uint16_t remote_port, std::string local_ip, uint16_t local_port)
      : ConnHolder({remote_ip, local_ip, remote_port, local_port, Side::ClientSide}) {}

  ConnHolder(std::string local_ip, uint16_t local_port) : ConnHolder({"", local_ip, 0, local_port, Side::ServerSide}) {}

  ~ConnHolder();

  void associate(Endpoint& e);
  void establish();
  void terminate();

 private:
  void passive_establish();
  void active_establish();

  ConnHolderConfig config;
  std::vector<Endpoint*> pending;
  std::unordered_map<int, Endpoint*> established;
  int ep_fd = -1;
};

}  // namespace dpx::trans::tcp
