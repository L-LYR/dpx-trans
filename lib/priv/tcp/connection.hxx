#pragma once

#include <netinet/in.h>

#include "priv/common.hxx"

namespace tcp {

class Endpoint;

class ConnectionHandle : public ConnectionHandleBase<ConnectionHandle, Endpoint> {
 public:
  ConnectionHandle(const ConnectionParam &info_);
  ~ConnectionHandle();

  // passive side
  void listen_and_accept();
  void wait_for_disconnect();

  // active side
  void connect();
  void disconnect();

 private:
  int conn_sock = -1;
};

}  // namespace tcp
