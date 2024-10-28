#pragma once

#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_pe.h>

#include <memory>

#include "util/doca_check.hxx"

struct DocaDevDeleter {
  void operator()(doca_dev *p) const { doca_check(doca_dev_close(p)); }
};
using DocaDev = std::unique_ptr<doca_dev, DocaDevDeleter>;

struct DocaDevRepDeleter {
  void operator()(doca_dev_rep *p) const { doca_check(doca_dev_rep_close(p)); }
};
using DocaDevRep = std::unique_ptr<doca_dev_rep, DocaDevRepDeleter>;

struct PeDeleter {
  void operator()(doca_pe *p) const { doca_check(doca_pe_destroy(p)); }
};
using Pe = std::unique_ptr<doca_pe, PeDeleter>;

struct ComchServerDeleter {
  void operator()(doca_comch_server *p) const { doca_check(doca_comch_server_destroy(p)); }
};
using ComchServer = std::unique_ptr<doca_comch_server, ComchServerDeleter>;

struct ComchClientDeleter {
  void operator()(doca_comch_client *p) const { doca_check(doca_comch_client_destroy(p)); }
};
using ComchClient = std::unique_ptr<doca_comch_client, ComchClientDeleter>;

using ComchConnection = doca_comch_connection *;

struct DocaComchConsumerDeleter {
  void operator()(doca_comch_consumer *p) const { doca_check(doca_comch_consumer_destroy(p)); }
};
using DocaComchConsumer = std::unique_ptr<doca_comch_consumer, DocaComchConsumerDeleter>;

struct DocaComchProducerDeleter {
  void operator()(doca_comch_producer *p) const { doca_check(doca_comch_producer_destroy(p)); }
};
using DocaComchProducer = std::unique_ptr<doca_comch_producer, DocaComchProducerDeleter>;

template <>
struct std::formatter<doca_ctx_states> : std::formatter<const char *> {
  template <typename Context>
  Context::iterator format(doca_ctx_states s, Context out) const {
    switch (s) {
      case DOCA_CTX_STATE_IDLE:
        return std::formatter<const char *>::format("Idle", out);
      case DOCA_CTX_STATE_STARTING:
        return std::formatter<const char *>::format("Starting", out);
      case DOCA_CTX_STATE_RUNNING:
        return std::formatter<const char *>::format("Running", out);
      case DOCA_CTX_STATE_STOPPING:
        return std::formatter<const char *>::format("Stopping", out);
    }
  }
};
