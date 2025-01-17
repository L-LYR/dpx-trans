#pragma once

#include <string>

namespace dpx {

void set_thread_name(std::string name);
std::string get_thread_name();
void bind_core(size_t core_idx);

}  // namespace dpx
