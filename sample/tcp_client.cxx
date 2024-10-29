#include <args.hxx>
#include <glaze/glaze.hpp>

#include "echo.hxx"
#include "priv/tcp/connection.hxx"
#include "priv/tcp/endpoint.hxx"
#include "util/logger.hxx"

using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::trace);

  args::ArgumentParser p("Sample tcp server");
  args::HelpFlag help(p, "help", "Display this help menu", {'h', "help"});
  args::ValueFlag<std::string> local_ip(p, "local ip", "local ip", {"local_ip"}, args::Options::Required);
  args::ValueFlag<std::string> remote_ip(p, "remote ip", "remote ip", {"remote_ip"}, args::Options::Required);
  args::ValueFlag<uint16_t> remote_port(p, "remote port", "remote port", {"remote_port"}, args::Options::Required);

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
  tcp::Connector c(args::get(remote_ip), args::get(remote_port));

  auto fn = [&](uint32_t i) {
    tcp::Endpoint<Side::ClientSide> e(2, 128);
    e.prepare();
    c.connect(e, args::get(local_ip), 10086 + i);
    e.run();
    auto echo_resp = e.call<EchoRpc>(PayloadType{.id = i, .message = "Hello"});
    INFO("{}", glz::write_json<>(echo_resp).value_or("Corrupted Payload!"));
    auto hello_resp = e.call<HelloRpc>("Hello");
    INFO("{}", hello_resp);
    std::this_thread::sleep_for(1s);
    e.stop();
  };

  std::jthread t1(fn, 1);
  std::jthread t2(fn, 2);
  return 0;
}
