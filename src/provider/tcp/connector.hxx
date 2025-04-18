#pragma once

#include <asio.hpp>
#include <cstdint>
#include <string>

#include "def.hxx"
#include "provider/tcp/endpoint.hxx"

namespace dpx::trans::tcp {

class Endpoint;

struct ConnectionInfo {
  std::string remote_ip;
  std::string local_ip;
  uint16_t remote_port;
  uint16_t local_port;
  Side side;
};

template <Side side>
class Connector {
 public:
  // active
  Connector(std::string remote_ip, uint16_t remote_port, std::string local_ip, uint16_t local_port)
    requires(side == Side::ClientSide)
      : info{remote_ip, local_ip, remote_port, local_port, Side::ClientSide} {}
  // passive
  Connector(std::string local_ip, uint16_t local_port)
    requires(side == Side::ServerSide)
      : info{"", local_ip, 0, local_port, Side::ServerSide} {}

  ~Connector() = default;

  std::vector<Endpoint> accept(asio::io_context& io, size_t n)
    requires(side == Side::ServerSide)
  {
    auto local = asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(info.local_ip), info.local_port);
    asio::ip::tcp::acceptor a(io, local);
    a.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    std::vector<Endpoint> conns;
    for (auto i = 0uz; i < n; i++) {
      asio::ip::tcp::socket s = a.accept();
      conns.emplace_back(std::move(s));
    }
    return conns;
  }

  std::vector<Endpoint> connect(asio::io_context& io, size_t n)
    requires(side == Side::ClientSide)
  {
    auto local = asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(info.local_ip), info.local_port);
    auto remote = asio::ip::tcp::endpoint(asio::ip::address_v4::from_string(info.remote_ip), info.remote_port);
    auto do_connect_one = [&]() {
      asio::ip::tcp::socket s(io);
      s.open(asio::ip::tcp::v4());
      s.bind(local);
      s.set_option(asio::ip::tcp::socket::reuse_address(true));
      s.connect(remote);
      return s;
    };
    std::vector<Endpoint> conns;
    for (auto i = 0uz; i < n; i++) {
      conns.emplace_back(do_connect_one());
    }
    return conns;
  }

 private:
  ConnectionInfo info;
};

}  // namespace dpx::trans::tcp
