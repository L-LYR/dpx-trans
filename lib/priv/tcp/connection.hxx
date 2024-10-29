#pragma once

#include <netinet/in.h>

#include <vector>

#include "priv/common.hxx"

namespace tcp {

template <Side side>
class Endpoint;

template <Side side>
using EndpointRef = std::reference_wrapper<Endpoint<side>>;

template <Side side>
using EndpointRefs = std::vector<EndpointRef<side>>;

class Acceptor : ConnectionHandleBase<Side::ServerSide> {
 public:
  Acceptor(std::string local_ip, uint16_t local_port);
  ~Acceptor();

  Acceptor &associate(EndpointRefs<Side::ServerSide> &&endpoints);

  void listen_and_accept();

 private:
  EndpointRefs<Side::ServerSide> pending_endpoints;
  int sock = -1;  // listening sock
};

class Connector : ConnectionHandleBase<Side::ClientSide> {
 public:
  Connector(std::string remote_ip, uint16_t remote_port);

  ~Connector() = default;

  void connect(Endpoint<Side::ClientSide> &e, std::string local_ip = "", uint16_t local_port = 0);

 private:
  sockaddr_in remote_addr_in = {};
};

}  // namespace tcp
