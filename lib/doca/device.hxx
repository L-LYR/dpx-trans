#pragma once

#include <doca_dev.h>

#include <glaze/core/common.hpp>
#include <glaze/core/meta.hpp>
#include <string_view>

#include "priv/defs.hxx"
#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

namespace doca {

namespace comch {

template <Side>
class Endpoint;

template <Side>
class ConnectionHandle;

}  // namespace comch

struct ComchCapability {
  struct {
    bool client_is_supported = false;
    bool server_is_supported = false;
    uint32_t max_clients_per_server = -1;
    uint32_t max_name_len = -1;
    uint32_t max_msg_size = -1;
    uint32_t max_send_tasks = -1;
    uint32_t max_recv_queue_size = -1;
  } ctrl_path;
  struct {
    struct {
      bool is_supported = false;
      uint32_t max_number = -1;
      uint32_t max_num_tasks = -1;
      uint32_t max_buf_size = -1;
      uint32_t max_buf_list_len = -1;
    } producer;
    struct {
      bool is_supported = false;
      uint32_t max_number = -1;
      uint32_t max_num_tasks = -1;
      uint32_t max_buf_size = -1;
      uint32_t max_buf_list_len = -1;
      uint32_t max_imm_data_len = -1;
    } consumer;
  } data_path;
};

class Device : Noncopyable, Nonmovable {
  template <Side>
  friend class comch::Endpoint;
  template <Side>
  friend class comch::ConnectionHandle;
  friend class Buffers;

 public:
  explicit Device(std::string_view dev_pci_addr);
  Device(std::string_view dev_pci_addr, std::string_view dev_rep_pci_addr, doca_devinfo_rep_filter filter);

  ~Device();

  bool run_on_dpu();

  ComchCapability probe_comch_params();

 private:
  doca_dev *dev = nullptr;
  doca_dev_rep *rep = nullptr;
};

}  // namespace doca
