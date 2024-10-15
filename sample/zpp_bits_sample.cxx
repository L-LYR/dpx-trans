#include "dbg.h"
#include "util/hex_dump.hxx"
#include "zpp_bits.h"

// Custom Byte Array
struct CustomByteArray {
  using value_type = char;

  CustomByteArray(size_t init_size = 1024)
      : raw_length(init_size), raw_capacity(init_size),
        raw_bytes(reinterpret_cast<value_type *>(calloc(init_size, 1))) {}
  ~CustomByteArray() { free(raw_bytes); }

  // zpp::bits::concept::byte_view
  value_type *data() { return raw_bytes; }
  const value_type *data() const { return raw_bytes; }
  size_t size() const { return raw_length; }
  value_type &operator[](size_t i) { return raw_bytes[i]; }
  const value_type &operator[](size_t i) const { return raw_bytes[i]; }

  // zpp::bits::in/out::resizable
  void resize(size_t new_size) {
    if (new_size < raw_capacity) {
      raw_length = new_size;
      return;
    }
    raw_capacity = new_size;
    raw_bytes = reinterpret_cast<value_type *>(realloc(raw_bytes, new_size));
  }
  void reserve(size_t needed_size) {
    raw_capacity = needed_size;
    raw_bytes = reinterpret_cast<value_type *>(realloc(raw_bytes, needed_size));
  }

private:
  size_t raw_length;
  size_t raw_capacity;
  value_type *raw_bytes;
};

struct sub {
  int32_t sss = 111;
  std::string xxx = "xxx";
};

struct data_to_serialize {
  int64_t x = 1;
  sub s;
  double ddd = 3.14;
  std::string kkk = "kkk";
  float y = 114.514;
  int32_t z = 114514;
};

int main() {
  CustomByteArray cba;
  zpp::bits::out o(cba);
  data_to_serialize d;
  auto errc [[maybe_unused]] = o(d);
  dbg(o.position());
  dbg(sizeof(d));
  std::cout << Hexdump(cba.data(), o.position()) << std::endl;
  std::cout << std::format("{}", Hexdump(cba.data(), o.position()))
            << std::endl;
  return 0;
}