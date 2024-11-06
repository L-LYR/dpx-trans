#include "doca/device.hxx"

#include "doca/check.hxx"

namespace {

doca_dev *open_dev(std::string_view pci_addr) {
  doca_devinfo **dev_list;
  uint32_t n_devs = 0;
  doca_check(doca_devinfo_create_list(&dev_list, &n_devs));
  for (auto devinfo : std::span<doca_devinfo *>(dev_list, n_devs)) {
    uint8_t is_equal = 0;
    doca_check(doca_devinfo_is_equal_pci_addr(devinfo, pci_addr.data(), &is_equal));
    if (is_equal) {
      doca_dev *dev = nullptr;
      doca_check(doca_dev_open(devinfo, &dev));
      doca_check(doca_devinfo_destroy_list(dev_list));
      return dev;
    }
  }
  die("Device {} not found", pci_addr);
}

doca_dev_rep *open_dev_rep(doca_dev *dev, std::string_view pci_addr, doca_devinfo_rep_filter filter) {
  uint32_t n_dev_reps = 0;
  doca_devinfo_rep **dev_rep_list = nullptr;
  doca_check(doca_devinfo_rep_create_list(dev, filter, &dev_rep_list, &n_dev_reps));
  for (auto &devinfo_rep : std::span<doca_devinfo_rep *>(dev_rep_list, n_dev_reps)) {
    uint8_t is_equal = 0;
    doca_check(doca_devinfo_rep_is_equal_pci_addr(devinfo_rep, pci_addr.data(), &is_equal));
    if (is_equal) {
      doca_dev_rep *dev_rep = nullptr;
      doca_check(doca_dev_rep_open(devinfo_rep, &dev_rep));
      doca_check(doca_devinfo_rep_destroy_list(dev_rep_list));
      return dev_rep;
    }
  }
  die("Device representor {} not found", pci_addr);
}

}  // namespace

namespace doca {

Device ::Device(std::string_view dev_pci_addr) : dev(open_dev(dev_pci_addr)) {}

Device::Device(std::string_view dev_pci_addr, std::string_view dev_rep_pci_addr, doca_devinfo_rep_filter filter)
    : dev(open_dev(dev_pci_addr)), rep(open_dev_rep(dev, dev_rep_pci_addr, filter)) {}

bool Device::run_on_dpu() {
  uint8_t support_representor = false;
  return rep != nullptr ||  // fast path
         doca_devinfo_rep_cap_is_filter_all_supported(doca_dev_as_devinfo(dev), &support_representor) == DOCA_SUCCESS;
}

Device::~Device() {
  if (rep != nullptr) {
    doca_check(doca_dev_rep_close(rep));
  }
  if (dev != nullptr) {
    doca_check(doca_dev_close(dev));
  }
}

}  // namespace doca