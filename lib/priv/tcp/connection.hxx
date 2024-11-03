#pragma once

#include <netinet/in.h>

#include <string>
#include <vector>

namespace tcp {

struct ConnectionParam {
  std::string remote_ip = "";
  std::string local_ip = "";
  uint16_t remote_port = 0;
  uint16_t local_port = 0;
};

class Endpoint;

class ConnectionHandle {
 public:
  using EndpointRef = std::reference_wrapper<Endpoint>;
  using EndpointRefs = std::vector<EndpointRef>;

 public:
  ConnectionHandle(const ConnectionParam& param_);

  ~ConnectionHandle();

  ConnectionHandle& associate(Endpoint& e);

  ConnectionHandle& associate(EndpointRefs&& es);

  void listen_and_accept();
  void wait_for_disconnect();

  void connect();
  void disconnect();

 private:
  int conn_sock = -1;
  const ConnectionParam& param;
  EndpointRefs pending_endpoints;
};

}  // namespace tcp
