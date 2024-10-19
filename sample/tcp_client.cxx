#include <args.hxx>
#include <glaze/glaze.hpp>

#include "tcp_common.hxx"

int main(int args, char* argv[]) {
  args::ArgumentParser p("Sample tcp server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> local_ip(p, "local ip", "local ip", {"local_ip"}, args::Options::Required);
  args::ValueFlag<uint16_t> local_port(p, "local port", "local port", {"local_port"}, args::Options::Required);
  args::ValueFlag<std::string> remote_ip(p, "remote ip", "remote ip", {"remote_ip"}, args::Options::Required);
  args::ValueFlag<uint16_t> remote_port(p, "remote port", "remote port", {"remote_port"}, args::Options::Required);

  try {
    p.ParseCLI(args, argv);
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
  Endpoint e(Buffers(10));
  Connector c(e, args::get(remote_ip), args::get(remote_port), args::get(local_ip), args::get(local_port));
  c.connect();
  auto resp = e.call<EchoRpc>(PayloadType{.id = 1, .message = "Hello"});
  INFO("{}", glz::write_json<>(resp).value_or("Corrupted Payload!"));
  return 0;
}
