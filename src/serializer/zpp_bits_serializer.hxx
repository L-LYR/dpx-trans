#pragma once

#include <zpp_bits.h>

#include "memory_region.hxx"

namespace dpx::trans {

class MemoryRegionWrapper final : public MemoryRegion {
 public:
  using value_type = uint8_t;  // zpp_bits inner traits
};

using ZppBitsSerializer = zpp::bits::out<MemoryRegionWrapper>;
using ZppBitsDeserializer = zpp::bits::in<MemoryRegionWrapper>;

}  // namespace dpx::trans
