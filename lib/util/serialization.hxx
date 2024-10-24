#pragma once

#include <zpp_bits.h>

#include "memory/simple_buffer.hxx"

using Serializer = zpp::bits::out<Buffer>;
using Deserializer = zpp::bits::in<Buffer>;