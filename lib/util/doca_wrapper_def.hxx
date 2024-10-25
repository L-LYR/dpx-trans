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

struct DocaPeDeleter {
  void operator()(doca_pe *p) const { doca_check(doca_pe_destroy(p)); }
};
using DocaPe = std::unique_ptr<doca_pe, DocaPeDeleter>;

struct DocaComchServerDeleter {
  void operator()(doca_comch_server *p) const { doca_check(doca_comch_server_destroy(p)); }
};
using DocaComchServer = std::unique_ptr<doca_comch_server, DocaComchServerDeleter>;

struct DocaComchClientDeleter {
  void operator()(doca_comch_client *p) const { doca_check(doca_comch_client_destroy(p)); }
};
using DocaComchClient = std::unique_ptr<doca_comch_client, DocaComchClientDeleter>;

using DocaComchConnection = doca_comch_connection *;

struct DocaComchConsumerDeleter {
  void operator()(doca_comch_consumer *p) const { doca_check(doca_comch_consumer_destroy(p)); }
};
using DocaComchConsumer = std::unique_ptr<doca_comch_consumer, DocaComchConsumerDeleter>;

struct DocaComchProducerDeleter {
  void operator()(doca_comch_producer *p) const { doca_check(doca_comch_producer_destroy(p)); }
};
using DocaComchProducer = std::unique_ptr<doca_comch_producer, DocaComchProducerDeleter>;
