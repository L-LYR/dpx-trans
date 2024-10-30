#pragma once

#include <netinet/in.h>

#include <vector>

#include "priv/common.hxx"

namespace tcp {

class Endpoint;

using EndpointRef = std::reference_wrapper<Endpoint>;

using EndpointRefs = std::vector<EndpointRef>;

class Acceptor : ConnectionHandleBase<Side::ServerSide> {
 public:
  Acceptor(std::string local_ip, uint16_t local_port);
  ~Acceptor();

  Acceptor &associate(EndpointRefs &&endpoints);

  void listen_and_accept();

 private:
  EndpointRefs pending_endpoints;
  int sock = -1;
};

class Connector : ConnectionHandleBase<Side::ClientSide> {
 public:
  Connector(std::string remote_ip, uint16_t remote_port);

  ~Connector() = default;

  void connect(Endpoint &e, std::string local_ip = "", uint16_t local_port = 0);

 private:
  sockaddr_in remote_addr_in = {};
};

}  // namespace tcp
