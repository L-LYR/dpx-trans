#pragma once

#include "concepts/rpc.hxx"
#include "def.hxx"
#include "provider/tcp/endpoint.hxx"

namespace dpx::trans {

template <Backend b>
class RpcTransport {
  // clang-format off
  using Endpoint =
    std::conditional_t<b == Backend::TCP,        tcp::Endpoint,
    // std::conditional_t<b == Backend::DOCA_Comch, doca::comch::Endpoint,
    // std::conditional_t<b == Backend::DOCA_RDMA,  doca::rdma::Endpoint,
                                                 void>
                                                //  >>
                                                 ;
  // clang-format on
};

}  // namespace dpx::trans
