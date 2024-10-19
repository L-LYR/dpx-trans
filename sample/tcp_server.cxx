#include <args.hxx>
#include <glaze/glaze.hpp>

#include "tcp_common.hxx"

int main(int args, char* argv[]) {
  args::ArgumentParser p("Sample tcp server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> local_ip(p, "local ip", "local ip", {"local_ip"}, args::Options::Required);
  args::ValueFlag<uint16_t> local_port(p, "local port", "local port", {"local_port"}, args::Options::Required);

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

  Endpoint e1(Buffers(10));
  Endpoint e2(Buffers(10));
  Acceptor a({e1, e2}, args::get(local_ip), args::get(local_port));
  std::jthread bg_acceptor([&a]() { a.listen(); });

  auto echo = [](Endpoint& e) {
    e.wait_and_ignore();  // wait for a connection
    auto req = e.read<PayloadType>();
    INFO("{}", glz::write_json<>(req).value_or("Corrupted Payload!"));
    req.id++;
    req.message += ", World";
    e.write(std::move(req));
  };

  std::jthread bg_e1(echo, std::ref(e1));
  std::jthread bg_e2(echo, std::ref(e2));
  // a.shutdown();
  return 0;
}
