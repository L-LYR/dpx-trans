#include <args.hxx>

#include "doca_comch_data_path_common.hxx"

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample comch server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> server_name(p, "server name", "server name", {"server_name"}, args::Options::Required);
  args::ValueFlag<std::string> dev_pci_address(p, "device pci address", "device pci address", {"dev_pci_address"},
                                               args::Options::Required);
  args::ValueFlag<std::string> rep_pci_address(p, "representor pci address", "representor pci address",
                                               {"rep_pci_address"}, args::Options::Required);

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

  Device d(args::get(dev_pci_address), args::get(rep_pci_address), DOCA_DEVINFO_REP_FILTER_NET);
  ctrl_path::Endpoint<Side::ServerSide> cpe(args::get(server_name), d);
  ctrl_path::Acceptor cpa;
  cpa.associate({cpe}).listen_and_accept();

  MmapBuffers buffers(d.dev, 16, 1024);
  data_path::Endpoint<Side::ServerSide> dpe(cpe, std::move(buffers));
  data_path::Acceptor dpa(cpe);
  dpa.associate({dpe}).listen_and_accept();

  return 0;
}