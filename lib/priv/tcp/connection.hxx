#pragma once

#include <netinet/in.h>

#include <memory>
#include <vector>

#include "priv/common.hxx"

namespace tcp {

class Endpoint;
using EndpointRef = std::reference_wrapper<Endpoint>;
using EndpointRefs = std::vector<EndpointRef>;

class Connection;
using ConnectionPtr = std::unique_ptr<Connection>;

class Connection : public ConnectionBase {
  friend class Acceptor;
  friend class Connector;
  friend class Endpoint;

 public:
  ~Connection();

 private:
  Connection(Side side_, int sock_);

  static void establish(Side side, int sock, Endpoint &e);

  int sock = -1;
};

class Acceptor : ConnectionHandleBase {
 public:
  Acceptor(std::string local_ip, uint16_t local_port);
  ~Acceptor();

  Acceptor &associate(EndpointRefs &&endpoints_);

  void listen_and_accept();

 private:
  EndpointRefs endpoints;
  int sock = -1;  // listening sock
};

class Connector : ConnectionHandleBase {
 public:
  Connector(std::string remote_ip, uint16_t remote_port);

  ~Connector() = default;

  void connect(Endpoint &e, std::string local_ip = "", uint16_t local_port = 0 /* 0 for INPORT_ANY */);

 private:
  sockaddr_in remote_addr_in = {};
};

}  // namespace tcp
