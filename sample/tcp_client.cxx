#include <args.hxx>
#include <glaze/glaze.hpp>

#include "tcp_common.hxx"

using namespace std::chrono_literals;

int main(int args, char* argv[]) {
  args::ArgumentParser p("Sample tcp server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> local_ip(p, "local ip", "local ip", {"local_ip"}, args::Options::Required);
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
  Connector c(args::get(remote_ip), args::get(remote_port));
  Endpoint e1(1, 128);
  Endpoint e2(1, 128);
  c.connect(e1, args::get(local_ip), 10087);
  c.connect(e2, args::get(local_ip), 10088);
  auto fn = [](Endpoint& e, uint32_t i) {
    auto resp = e.call<EchoRpc>(PayloadType{.id = i, .message = "Hello"});
    INFO("{}", glz::write_json<>(resp).value_or("Corrupted Payload!"));
  };
  std::jthread t1(fn, std::ref(e1), 1);
  std::jthread t2(fn, std::ref(e2), 2);
  return 0;
}
