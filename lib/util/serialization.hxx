#pragma once

#include <zpp_bits.h>

#include "memory/simple_buffer.hxx"

using Serializer = zpp::bits::out<BufferBase>;
using Deserializer = zpp::bits::in<BufferBase>;