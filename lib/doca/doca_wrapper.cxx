#include "util/doca_wrapper.hxx"

#include "memory/doca_simple_buffer.hxx"

namespace doca_wrapper {


ComchServer create_comch_server(DocaDev &dev, DocaDevRep &dev_rep, std::string_view name) {
  doca_comch_server *server = nullptr;
  return ComchServer(server);
}

ComchClient create_comch_client(DocaDev &dev, std::string_view name) {
  doca_comch_client *client = nullptr;
  doca_check(doca_comch_client_create(dev.get(), name.data(), &client));
  return ComchClient(client);
}

DocaComchProducer create_comch_producer(ComchConnection connection) {
  doca_comch_producer *producer = nullptr;
  doca_check(doca_comch_producer_create(connection, &producer));
  return DocaComchProducer(producer);
}

Pe create_pe() {
  doca_pe *pe = nullptr;
  doca_check(doca_pe_create(&pe));
  return Pe(pe);
}

uint32_t device_comch_max_msg_size(DocaDev &dev) {
  uint32_t size = 0;
  doca_check(doca_comch_cap_get_max_msg_size(doca_dev_as_devinfo(dev.get()), &size));
  return size;
}

uint32_t device_comch_max_recv_queue_size(DocaDev &dev) {
  uint32_t size = 0;
  doca_check(doca_comch_cap_get_max_recv_queue_size(doca_dev_as_devinfo(dev.get()), &size));
  return size;
}

uint32_t get_comch_consumer_id(DocaComchConsumer &consumer) {
  uint32_t id = 0;
  doca_check(doca_comch_consumer_get_id(consumer.get(), &id));
  return id;
}

DocaComchConsumer create_comch_consumer(ComchConnection connection, MmapBuffers &buffers) {
  doca_comch_consumer *consumer = nullptr;
  doca_check(doca_comch_consumer_create(connection, buffers.underlying(), &consumer));
  return DocaComchConsumer(consumer);
}

}  // namespace doca_wrapper