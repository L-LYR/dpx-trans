#include "priv/context.hxx"

#include "memory/simple_buffer.hxx"

OpContext::OpContext(Op op_, BufferBase &buf_) : op(op_), buf(buf_), len(buf.size()) {}

OpContext::OpContext(Op op_, BufferBase &buf_, size_t len_) : op(op_), buf(buf_), len(len_) {}
