#include <args.hxx>

#include "doca_comch_common.hxx"

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

  // DocaComch comch(args::get(server_name), args::get(dev_pci_address), args::get(rep_pci_address));
  // Endpoint e(comch, 16, 1024);
  // Acceptor a(comch);
  // a.associate({e}).listen_and_accept();
  // e.progress();

  return 0;
}