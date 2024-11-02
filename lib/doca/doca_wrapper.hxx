#pragma once

#include "priv/common.hxx"
#include "util/doca_wrapper_def.hxx"

class MmapBuffers;

namespace doca_wrapper {

DocaDev open_dev(std::string_view pci_addr);
DocaDevRep open_dev_rep(DocaDev &dev, std::string_view pci_addr,
                        doca_devinfo_rep_filter filter = DOCA_DEVINFO_REP_FILTER_ALL);
ComchServer create_comch_server(DocaDev &dev, DocaDevRep &dev_rep, std::string_view name);
ComchClient create_comch_client(DocaDev &dev, std::string_view name);
DocaComchProducer create_comch_producer(ComchConnection connection);
Pe create_pe();
uint32_t device_comch_max_msg_size(DocaDev &dev);
uint32_t device_comch_max_recv_queue_size(DocaDev &dev);
uint32_t get_comch_consumer_id(DocaComchConsumer &consumer);
DocaComchConsumer create_comch_consumer(ComchConnection connection, MmapBuffers &buffers);



}  // namespace doca_wrapper
