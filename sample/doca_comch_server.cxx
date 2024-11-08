#include <args.hxx>
#include <glaze/glaze.hpp>

#include "echo.hxx"
#include "priv/transport.hxx"

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample doca comch server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> server_name(p, "server name", "server name", {"server_name"}, args::Options::Required);
  args::ValueFlag<std::string> dev_pci_address(p, "device pci address", "device pci address", {"dev_pci_address"},
                                               args::Options::Required);
  args::ValueFlag<std::string> rep_pci_address(p, "representor pci address", "representor pci address",
                                               {"rep_pci_address"}, args::Options::Required);
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

  Config<Backend::DOCA_Comch> c{
      .queue_depth = args::get(n_worker), .max_rpc_msg_size = 4096, .conn_param = {.name = "sample"}};
  doca::Device dev(args::get(dev_pci_address), args::get(rep_pci_address), DOCA_DEVINFO_REP_FILTER_NET);
  Transport<Backend::DOCA_Comch, Side::ServerSide, EchoRpc> t(dev, c);
  auto echo = [&]() {
    TransportGuard g(t);
    t.serve();
  };
  std::jthread bg_e1(echo);
  return 0;
}