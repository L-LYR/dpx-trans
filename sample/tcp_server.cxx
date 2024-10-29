#include <args.hxx>
#include <glaze/glaze.hpp>

#include "echo.hxx"
#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample tcp server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> local_ip(p, "local ip", "local ip", {"local_ip"}, args::Options::Required);
  args::ValueFlag<uint16_t> local_port(p, "local port", "local port", {"local_port"}, args::Options::Required);

  try {
    p.ParseCLI(argc, argv);
  } catch (args::Help) {
    std::cout << p;
    return 0;
  } catch (args::ParseError e) {
    std::cerr << e.what() << std::endl << std::endl << p;
    return -1;
  } catch (args::ValidationError e) {
    std::cerr << e.what() << std::endl << std::endl << p;
    return -1;
  }

  tcp::Endpoint<Side::ServerSide> e1(16, 128);
  tcp::Endpoint<Side::ServerSide> e2(16, 128);
  tcp::Acceptor a(args::get(local_ip), args::get(local_port));
  e1.prepare();
  e2.prepare();
  a.associate({e1, e2}).listen_and_accept();

  auto echo = [](tcp::Endpoint<Side::ServerSide>& e) {
    e.run();
    e.serve<EchoRpc, HelloRpc>(4);
    // e.stop();
  };

  std::jthread bg_e1(echo, std::ref(e1));
  std::jthread bg_e2(echo, std::ref(e2));

  return 0;
}
