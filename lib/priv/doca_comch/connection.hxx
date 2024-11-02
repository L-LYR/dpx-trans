#pragma once

#include "priv/common.hxx"

namespace doca::comch {

struct ConnectionParam : ConnectionCommonParam {
  std::string name;
};

}  // namespace doca::comch

namespace doca::comch::ctrl_path {

class Endpoint;

class ConnectionHandle : public ConnectionHandleBase<ConnectionHandle, Endpoint, ConnectionParam> {
 public:
  ConnectionHandle(const ConnectionParam &param_);
  ~ConnectionHandle();

  void listen_and_accept();
  void wait_for_disconnect();

  void connect();
  void disconnect();

 private:
  void progress_all_until(std::function<bool(Endpoint &e)> &&predictor);
};

}  // namespace doca::comch::ctrl_path