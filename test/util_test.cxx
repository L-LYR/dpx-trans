#include <catch2/catch_test_macros.hpp>
#include <iostream>

#include "util/hex_dump.hxx"

using dpx::trans::Hexdump;

TEST_CASE("Hexdump") {
  SECTION("Nullptr") {
    const char *p = nullptr;
    REQUIRE_NOTHROW(std::cout << Hexdump(p, 0) << std::endl);
  }
  SECTION("Nullptr with length") {
    const char *p = nullptr;
    REQUIRE_THROWS(std::cout << Hexdump(p, 10) << std::endl);
  }
  SECTION("Empty") {
    const char *p = "";
    std::cout << Hexdump(p, strlen(p)) << std::endl;
  }
  SECTION("Short") {
    const char *p = "A";
    std::cout << Hexdump(p, strlen(p)) << std::endl;
  }
  SECTION("Long") {
    const char *p = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::cout << Hexdump(p, strlen(p)) << std::endl;
  }
}
