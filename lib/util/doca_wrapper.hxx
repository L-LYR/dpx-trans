#pragma once

#include "priv/common.hxx"
#include "util/doca_wrapper_def.hxx"

class MmapBuffers;

namespace doca_wrapper {

DocaDev open_doca_dev(std::string_view pci_addr);
DocaDevRep open_doca_dev_rep(DocaDev &dev, std::string_view pci_addr,
                             enum doca_devinfo_rep_filter filter = DOCA_DEVINFO_REP_FILTER_ALL);
DocaComchServer create_comch_server(DocaDev &dev, DocaDevRep &dev_rep, std::string_view name);
DocaComchClient create_comch_client(DocaDev &dev, std::string_view name);
DocaComchProducer create_comch_producer(DocaComchConnection connection);
DocaPe create_pe();
uint32_t device_comch_max_msg_size(DocaDev &dev);
uint32_t device_comch_max_recv_queue_size(DocaDev &dev);
uint32_t get_comch_consumer_id(DocaComchConsumer &consumer);
DocaComchConsumer create_comch_consumer(DocaComchConnection connection, MmapBuffers &buffers);

template <Side side>
inline void *get_user_data_from_connection(DocaComchConnection c) {
  doca_data user_data(nullptr);
  if constexpr (side == Side::ServerSide) {
    doca_check(doca_ctx_get_user_data(doca_comch_server_as_ctx(doca_comch_server_get_server_ctx(c)), &user_data));
  } else if constexpr (side == Side::ClientSide) {
    doca_check(doca_ctx_get_user_data(doca_comch_client_as_ctx(doca_comch_client_get_client_ctx(c)), &user_data));
  } else {
    static_assert(false, "Wrong side!");
  }
  return user_data.ptr;
}

}  // namespace doca_wrapper
