#include <args.hxx>
#include <glaze/glaze.hpp>

#include "echo.hxx"
#include "priv/transport.hxx"
#include "util/logger.hxx"
#include "util/timer.hxx"

using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample doca comch client");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> dev_pci_address(p, "pci address", "pci address", {"pci_address"},
                                               args::Options::Required);
  args::ValueFlag<std::string> server_name(p, "server name", "server name", {"server_name"}, args::Options::Required);
  args::ValueFlag<uint32_t> n_caller(p, "n caller", "n caller", {"n_caller"}, 1);

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
  Config<Backend::DOCA_Comch> c{
      .queue_depth = args::get(n_caller) * 2, .max_rpc_msg_size = 4080, .conn_param = {.name = "sample"}};
  doca::Device dev(args::get(dev_pci_address));
  Transport<Backend::DOCA_Comch, Side::ClientSide, EchoRpc> t(dev, c);

  auto call_fn = [&]() {
    for (auto i = 0; i < 0; i++) {
      auto echo_resp = t.call<EchoRpc>(payload_4k);
      auto resp = echo_resp.get();
    }
  };

  auto fn = [&]() {
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
