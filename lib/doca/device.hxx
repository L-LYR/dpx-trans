#pragma once

#include <doca_dev.h>

#include <string_view>

#include "util/noncopyable.hxx"
#include "util/nonmovable.hxx"

namespace doca {

namespace comch::ctrl_path {
class Endpoint;
}

class Device : Noncopyable, Nonmovable {
  friend class comch::ctrl_path::Endpoint;
  friend class Buffers;

 public:
  explicit Device(std::string_view dev_pci_addr);
  Device(std::string_view dev_pci_addr, std::string_view dev_rep_pci_addr, doca_devinfo_rep_filter filter);

  ~Device();

  bool run_on_dpu();

 private:
  doca_dev *dev = nullptr;
  doca_dev_rep *rep = nullptr;
};

}  // namespace doca