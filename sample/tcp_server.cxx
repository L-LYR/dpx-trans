#include <args.hxx>
#include <glaze/glaze.hpp>

#include "echo.hxx"
#include "priv/transport.hxx"

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample tcp server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> local_ip(p, "local ip", "local ip", {"local_ip"}, args::Options::Required);
  args::ValueFlag<uint16_t> local_port(p, "local port", "local port", {"local_port"}, args::Options::Required);
  args::ValueFlag<uint32_t> n_worker(p, "n worker", "n worker", {"n_worker"}, 1);

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

  Config<Backend::TCP> c{
      .queue_depth = args::get(n_worker),
      .max_rpc_msg_size = 4096,
      .conn_param =
          {
              .local_ip = args::get(local_ip),
              .local_port = args::get(local_port),
          },
  };

  Transport<Backend::TCP, Side::ServerSide, EchoRpc> t(c);

  auto echo = [&]() {
    TransportGuard g(t);
    t.serve();
  };

  std::jthread bg_e1(echo);

  return 0;
}