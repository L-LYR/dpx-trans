#include "util/doca_wrapper.hxx"

#include "memory/doca_simple_buffer.hxx"

namespace doca_wrapper {

uint32_t get_comch_consumer_id(DocaComchConsumer &consumer) {
  uint32_t id = 0;
  doca_check(doca_comch_consumer_get_id(consumer.get(), &id));
  return id;
}

}  // namespace doca_wrapper