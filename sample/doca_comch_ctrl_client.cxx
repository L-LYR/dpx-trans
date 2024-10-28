#include <args.hxx>

#include "doca_comch_ctrl_common.hxx"

using namespace ctrl_path;

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample comch client");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> server_name(p, "server name", "server name", {"server_name"}, args::Options::Required);
  args::ValueFlag<std::string> dev_pci_address(p, "pci address", "pci address", {"pci_address"},
                                               args::Options::Required);

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

  Device d(get(dev_pci_address));
  Endpoint<Side::ClientSide> e(args::get(server_name), d);
  Connector c;
  c.connect(e);
  return 0;
}