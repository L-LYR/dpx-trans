#include "util/doca_wrapper.hxx"

#include "memory/doca_simple_buffer.hxx"

namespace doca_wrapper {

DocaDev open_doca_dev(std::string_view pci_addr) {
  doca_devinfo **dev_list;
  uint32_t n_devs = 0;
  doca_check(doca_devinfo_create_list(&dev_list, &n_devs));
  for (auto devinfo : std::span<doca_devinfo *>(dev_list, n_devs)) {
    uint8_t is_equal = 0;
    doca_check(doca_devinfo_is_equal_pci_addr(devinfo, pci_addr.data(), &is_equal));
    if (is_equal) {
      doca_dev *dev = nullptr;
      doca_check(doca_dev_open(devinfo, &dev));
      doca_devinfo_destroy_list(dev_list);
      return DocaDev(dev);
    }
  }
  die("Device {} not found", pci_addr);
}

DocaDevRep open_doca_dev_rep(DocaDev &dev, std::string_view pci_addr, enum doca_devinfo_rep_filter filter) {
  uint32_t n_dev_reps = 0;
  doca_devinfo_rep **dev_rep_list = nullptr;
  doca_check(doca_devinfo_rep_create_list(dev.get(), filter, &dev_rep_list, &n_dev_reps));
  for (auto &devinfo_rep : std::span<doca_devinfo_rep *>(dev_rep_list, n_dev_reps)) {
    uint8_t is_equal = 0;
    doca_check(doca_devinfo_rep_is_equal_pci_addr(devinfo_rep, pci_addr.data(), &is_equal));
    if (is_equal) {
      doca_dev_rep *dev_rep = nullptr;
      doca_check(doca_dev_rep_open(devinfo_rep, &dev_rep));
      doca_devinfo_rep_destroy_list(dev_rep_list);
      return DocaDevRep(dev_rep);
    }
  }
  die("Device representor {} not found", pci_addr);
}

DocaComchServer create_comch_server(DocaDev &dev, DocaDevRep &dev_rep, std::string_view name) {
  doca_comch_server *server = nullptr;
  doca_check(doca_comch_server_create(dev.get(), dev_rep.get(), name.data(), &server));
  return DocaComchServer(server);
}

DocaComchClient create_comch_client(DocaDev &dev, std::string_view name) {
  doca_comch_client *client = nullptr;
  doca_check(doca_comch_client_create(dev.get(), name.data(), &client));
  return DocaComchClient(client);
}

DocaComchProducer create_comch_producer(DocaComchConnection connection) {
  doca_comch_producer *producer = nullptr;
  doca_check(doca_comch_producer_create(connection, &producer));
  return DocaComchProducer(producer);
}

DocaPe create_pe() {
  doca_pe *pe = nullptr;
  doca_check(doca_pe_create(&pe));
  return DocaPe(pe);
}

uint32_t comch_server_max_msg_size(DocaDev &dev) {
  uint32_t size = 0;
  doca_check(doca_comch_cap_get_max_msg_size(doca_dev_as_devinfo(dev.get()), &size));
  return size;
}

uint32_t comch_server_recv_queue_size(DocaComchServer &server) {
  uint32_t size = 0;
  doca_check(doca_comch_server_get_recv_queue_size(server.get(), &size));
  return size;
}

uint32_t comch_client_max_msg_size(DocaComchClient &client) {
  uint32_t size = 0;
  doca_check(doca_comch_client_get_max_msg_size(client.get(), &size));
  return size;
}

uint32_t comch_client_recv_queue_size(DocaComchClient &client) {
  uint32_t size = 0;
  doca_check(doca_comch_client_get_recv_queue_size(client.get(), &size));
  return size;
}

uint32_t get_comch_consumer_id(DocaComchConsumer &consumer) {
  uint32_t id = 0;
  doca_check(doca_comch_consumer_get_id(consumer.get(), &id));
  return id;
}

DocaComchConsumer create_comch_consumer(DocaComchConnection connection, MmapBuffers &buffers) {
  doca_comch_consumer *consumer = nullptr;
  doca_check(doca_comch_consumer_create(connection, buffers.underlying(), &consumer));
  return DocaComchConsumer(consumer);
}

}  // namespace doca_wrapper