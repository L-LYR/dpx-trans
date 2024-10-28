#include <args.hxx>

#include "doca_comch_data_path_common.hxx"

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
  ctrl_path::Endpoint<Side::ClientSide> cpe(args::get(server_name), d);
  ctrl_path::Connector cpc;
  cpc.connect(cpe);

  MmapBuffers buffers(d.dev, 16, 1024);
  data_path::Endpoint<Side::ClientSide> dpe(cpe, std::move(buffers));
  data_path::Connector dpc(cpe);
  dpc.connect(dpe);

  return 0;
}