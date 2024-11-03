#include <args.hxx>
#include <glaze/glaze.hpp>

#include "echo.hxx"
#include "priv/transport.hxx"
#include "util/timer.hxx"

using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample rdma server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> local_ip(p, "local ip", "local ip", {"local_ip"}, args::Options::Required);
  args::ValueFlag<std::string> remote_ip(p, "remote ip", "remote ip", {"remote_ip"}, args::Options::Required);
  args::ValueFlag<uint16_t> remote_port(p, "remote port", "remote port", {"remote_port"}, args::Options::Required);
  args::ValueFlag<uint32_t> n_caller(p, "n caller", "n caller", {"n_caller"}, 2);

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

  Config<Backend::Verbs> c{
      .queue_depth = args::get(n_caller) * 2,
      .max_rpc_msg_size = 4096,
      .conn_param =
          {
              .remote_ip = args::get(remote_ip),
              .local_ip = args::get(local_ip),
              .remote_port = args::get(remote_port),
          },
  };

  Transport<Backend::Verbs, Side::ClientSide, EchoRpc> t(c);

  auto call_fn = [&]() {
    for (auto i = 0; i < 10000; i++) {
      auto echo_resp = t.call<EchoRpc>(payload_4k);
      auto resp = echo_resp.get();
    }
  };

  auto fn = [&]() {
    std::this_thread::sleep_for(1s);
    TransportGuard g(t);
    Timer tt;
    std::vector<boost::fibers::fiber> callers;
    callers.reserve(args::get(n_caller));
    for (auto i = 0uz; i < callers.capacity(); ++i) {
      callers.emplace_back(call_fn);
    }
    for (auto& caller : callers) {
      caller.join();
    }
    INFO("{}us", tt.elapsed_us());
  };

  std::jthread t1(fn);

  return 0;
}