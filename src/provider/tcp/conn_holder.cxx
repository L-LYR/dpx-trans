#include "provider/tcp/conn_holder.hxx"

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "provider/tcp/endpoint.hxx"
#include "util/fatal.hxx"
#include "util/logger.hxx"

namespace dpx::trans::tcp {

namespace {

inline void close_fd(int fd) {
  if (fd <= 0) {
    return;
  }
  if (auto ec = close(fd); ec < 0) {
    die("Fail to close fd {}, errno: {}", fd, errno);
  }
}

inline int create_socket() {
  int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sock < 0) {
    die("Fail to create server side socket, errno: {}", errno);
  }
  bool enable = true;
  if (auto ec = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &enable, sizeof(int)); ec < 0) {
    die("Fail to set socket options, errno: {}", errno);
  }
  return sock;
}

inline void bind_socket(int sock, std::string_view ip, uint16_t port) {
  auto ip_in = (ip.empty() ? INADDR_ANY : inet_addr(ip.data()));
  if (ip_in == INADDR_NONE) {
    die("Wrong format: {}", ip);
  }
  auto addr_in = sockaddr_in{
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = {.s_addr = ip_in},
      .sin_zero = {},
  };
  if (auto ec = bind(sock, reinterpret_cast<const sockaddr*>(&addr_in), sizeof(addr_in)); ec < 0) {
    die("Fail to bind {}:{}, errno: {}", ip, port, errno);
  }
}

}  // namespace

ConnHolder::ConnHolder(const ConnHolderConfig& config_) : config(config_) {
  ep_fd = epoll_create(16);
  if (ep_fd < 0) {
    die("Fail to create epoll, errno: {}", errno);
  }
}

ConnHolder::~ConnHolder() {
  if (!established.empty()) {
    terminate();
  }
  close_fd(ep_fd);
  close_fd(listen_sock);
}

void ConnHolder::associate(Endpoint& e) { pending.push_back(&e); }

void ConnHolder::establish() {
  std::vector<epoll_event> es;
  es.reserve(1 + pending.size());
  auto add_sock_event = [this, &es](uint32_t event, int sock) {
    es.push_back(epoll_event{.events = event, .data.fd = sock});
    if (auto ec = epoll_ctl(ep_fd, EPOLL_CTL_ADD, sock, &es.back()); ec < 0) {
      die("Fail to add listen event, errno: {}", errno);
    }
  };

  if (config.s == Side::ServerSide) {
    INFO("Start listening at {}:{}", config.local_ip, config.local_port);
    listen_sock = create_socket();
    bind_socket(listen_sock, config.local_ip, config.local_port);
    if (auto ec = listen(listen_sock, 8); ec < 0) {
      die("Fail to listen {}, errno: {}", listen_sock, errno);
    }
    add_sock_event(EPOLLIN, listen_sock);
  }

  if (config.s == Side::ClientSide) {
    auto remote_addr_in = sockaddr_in{
        .sin_family = AF_INET,
        .sin_port = htons(config.remote_port),
        .sin_addr = {.s_addr = inet_addr(config.remote_ip.data())},
        .sin_zero = {},
    };
    for (auto i = 0uz; i < pending.size(); i++) {
      auto sock = create_socket();
      bind_socket(sock, config.local_ip, 0);
      if (auto ec = connect(sock, reinterpret_cast<sockaddr*>(&remote_addr_in), sizeof(remote_addr_in));
          ec < 0 && errno != EINPROGRESS) {
        die("Fail to connect sock {} with {}:{}, errno: {}", sock, config.remote_ip, config.remote_port, errno);
      } else {
        add_sock_event(EPOLLOUT, sock);
      }
    }
  }

  while (!pending.empty() && !pending.empty()) {
    epoll_event e = {};
    auto ec = epoll_wait(ep_fd, &e, 1, 1000);
    if (ec < 0) {
      if (errno == EINTR) {
        continue;
      }
      die("Fail to wait epoll, errno: {}", errno);
    } else if (ec == 0) {
      // do nothing
      INFO("Pending {}", pending.size());
      continue;
    }
    assert(ec == 1);
    if (e.data.fd == listen_sock) {
      sockaddr_in remote_addr = {};
      socklen_t len = sizeof(struct sockaddr);
      auto client_sock = accept(listen_sock, reinterpret_cast<sockaddr*>(&remote_addr), &len);
      if (client_sock < 0) {
        die("Fail to accept, errno: {}", errno);
      }
      if (auto ec = fcntl(client_sock, F_SETFL, fcntl(client_sock, F_GETFD, 0) | O_NONBLOCK); ec < 0) {
        die("Fail to set client sock non-blocking, errno: {}", errno);
      }
      INFO("Establish one connection: {}:{}<->{}:{}, fd: {}", config.local_ip, config.local_port,
           inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port), client_sock);
      pending.back()->conn = client_sock;
      established.emplace(client_sock, pending.back());
      pending.pop_back();
    } else {  // TODO: do some check here.
      int connect_errno = 0;
      socklen_t len = sizeof(connect_errno);
      if (auto ec = getsockopt(e.data.fd, SOL_SOCKET, SO_ERROR, (void*)(&connect_errno), &len); ec < 0) {
        die("Fail to get socket options, errno: {}", errno);
      }

      if (connect_errno != 0) {
        die("Fail to connection, errno: {}", connect_errno);
      }

      INFO("Connect with {}:{}, fd: {}", config.remote_ip, config.remote_port, e.data.fd);
      pending.back()->conn = e.data.fd;
      established.emplace(e.data.fd, pending.back());
      pending.pop_back();
    }
  }

  INFO("Establish {} connections", established.size());
}

void ConnHolder::terminate() {
  for (auto [sock, e] : established) {
    close_fd(sock);
    e->conn = -1;
  }
  established.clear();
}

}  // namespace dpx::trans::tcp
