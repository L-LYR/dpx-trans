#include <doca_buf.h>
#include <doca_buf_pool.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include "doca/check.hxx"

std::string pci_addr = "0000:03:00.0";
doca_dev *dev = nullptr;
uint32_t n_devs = 0;
doca_devinfo **dev_list;

std::string rep_pci_addr = "0000:99:00.0";
doca_dev_rep *rep = nullptr;
uint32_t n_dev_reps = 0;
doca_devinfo_rep **dev_rep_list;

size_t len = 8192;
size_t piece_len = 4096;
uint8_t buffer[8192] = {};
doca_mmap *mmap = nullptr;

doca_buf_pool *pool = nullptr;

doca_buf *send_buf = nullptr;

doca_buf *recv_buf = nullptr;

doca_pe *pe = nullptr;

std::string name = "debug";
doca_comch_server *s;
doca_comch_connection *conn = nullptr;

uint32_t max_msg_size = 0;
uint32_t recv_queue_size = 0;

template <typename Fn>
void poll_until(Fn &&predictor) {
  while (!predictor) {
    doca_pe_progress(pe);
  }
};

bool s_running = false;
bool s_stop = false;

doca_comch_producer *pro = nullptr;
bool pro_running = false;
bool pro_stop = false;

doca_comch_consumer *con = nullptr;
bool con_running = false;
bool con_stop = false;

uint32_t remote_consumer_id = 0;

void server_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);
void connect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t);
void disconnect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *, uint8_t);
void new_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);
void expired_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);
void task_completion_cb(doca_comch_task_send *, doca_data, doca_data);
void task_error_cb(doca_comch_task_send *, doca_data, doca_data);
void recv_event_cb(struct doca_comch_event_msg_recv *, uint8_t *, uint32_t, struct doca_comch_connection *);
void producer_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);
void post_send_cb(struct doca_comch_producer_task_send *, union doca_data, union doca_data);
void post_send_err_cb(struct doca_comch_producer_task_send *, union doca_data, union doca_data);
void consumer_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);
void post_recv_cb(struct doca_comch_consumer_task_post_recv *, union doca_data, union doca_data);
void post_recv_err_cb(struct doca_comch_consumer_task_post_recv *, union doca_data, union doca_data);

int main() {
  doca_check(doca_devinfo_create_list(&dev_list, &n_devs));
  for (auto devinfo : std::span<doca_devinfo *>(dev_list, n_devs)) {
    uint8_t is_equal = 0;
    doca_check(doca_devinfo_is_equal_pci_addr(devinfo, pci_addr.data(), &is_equal));
    if (is_equal) {
      doca_check(doca_dev_open(devinfo, &dev));
      break;
    }
  }
  doca_check(doca_devinfo_destroy_list(dev_list));
  if (dev == nullptr) {
    die("Device {} not found", pci_addr);
  }

  doca_check(doca_devinfo_rep_create_list(dev, DOCA_DEVINFO_REP_FILTER_NET, &dev_rep_list, &n_dev_reps));
  for (auto &devinfo_rep : std::span<doca_devinfo_rep *>(dev_rep_list, n_dev_reps)) {
    uint8_t is_equal = 0;
    doca_check(doca_devinfo_rep_is_equal_pci_addr(devinfo_rep, rep_pci_addr.data(), &is_equal));
    if (is_equal) {
      doca_check(doca_dev_rep_open(devinfo_rep, &rep));
      break;
    }
  }
  doca_check(doca_devinfo_rep_destroy_list(dev_rep_list));
  if (rep == nullptr) {
    die("Device representor {} not found", rep_pci_addr);
  }

  doca_check(doca_mmap_create(&mmap));
  doca_check(doca_mmap_add_dev(mmap, dev));
  doca_check(doca_mmap_set_permissions(mmap, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
  doca_check(doca_mmap_set_memrange(mmap, buffer, len));
  doca_check(doca_mmap_start(mmap));

  doca_check(doca_buf_pool_create(len / piece_len, piece_len, mmap, &pool));
  doca_check(doca_buf_pool_start(pool));

  doca_check(doca_buf_pool_buf_alloc(pool, &send_buf));
  doca_check(doca_buf_set_data_len(send_buf, piece_len));

  doca_check(doca_buf_pool_buf_alloc(pool, &recv_buf));
  doca_check(doca_buf_set_data_len(recv_buf, piece_len));

  doca_check(doca_pe_create(&pe));

  doca_check(doca_comch_cap_get_max_msg_size(doca_dev_as_devinfo(dev), &max_msg_size));
  doca_check(doca_comch_cap_get_max_recv_queue_size(doca_dev_as_devinfo(dev), &recv_queue_size));

  doca_check(doca_comch_server_create(dev, rep, name.data(), &s));
  auto s_ctx = doca_comch_server_as_ctx(s);
  doca_check(doca_comch_server_task_send_set_conf(s, task_completion_cb, task_error_cb, recv_queue_size));
  doca_check(doca_comch_server_event_connection_status_changed_register(s, connect_event_cb, disconnect_event_cb));
  doca_check(doca_comch_server_event_msg_recv_register(s, recv_event_cb));
  doca_check(doca_comch_server_event_consumer_register(s, new_consumer_event_cb, expired_consumer_event_cb));
  doca_check(doca_comch_server_set_max_msg_size(s, max_msg_size));
  doca_check(doca_comch_server_set_recv_queue_size(s, recv_queue_size));
  doca_check(doca_pe_connect_ctx(pe, s_ctx));
  doca_check(doca_ctx_set_state_changed_cb(s_ctx, server_state_change_cb));
  doca_check(doca_ctx_start(s_ctx));

  poll_until([]() { return s_running; });

  doca_check(doca_comch_producer_create(conn, &pro));
  auto pro_ctx = doca_comch_producer_as_ctx(pro);
  doca_check(doca_pe_connect_ctx(pe, pro_ctx));
  doca_check(doca_ctx_set_state_changed_cb(pro_ctx, producer_state_change_cb));
  doca_check(doca_comch_producer_task_send_set_conf(pro, post_send_cb, post_send_err_cb, 32));
  doca_check(doca_ctx_start(pro_ctx));

  poll_until([]() { return pro_running; });

  doca_check(doca_comch_consumer_create(conn, mmap, &con));
  auto con_ctx = doca_comch_consumer_as_ctx(con);
  doca_check(doca_pe_connect_ctx(pe, con_ctx));
  doca_check(doca_ctx_set_state_changed_cb(con_ctx, consumer_state_change_cb));
  doca_check(doca_comch_consumer_task_post_recv_set_conf(con, post_recv_cb, post_recv_err_cb, 32));
  doca_check_ext(doca_ctx_start(con_ctx), DOCA_ERROR_IN_PROGRESS);

  poll_until([]() { return con_running; });

  poll_until([]() { return remote_consumer_id != 0; });

  doca_check(doca_ctx_stop(pro_ctx));
  poll_until([]() { return pro_stop; });

  doca_check(doca_ctx_stop(con_ctx));
  poll_until([]() { return con_stop; });

  doca_check(doca_ctx_stop(s_ctx));
  poll_until([]() { return s_stop; });

  doca_check(doca_comch_server_destroy(s));

  doca_check(doca_pe_destroy(pe));

  doca_check(doca_buf_dec_refcount(send_buf, nullptr));
  doca_check(doca_buf_dec_refcount(recv_buf, nullptr));

  doca_check(doca_buf_pool_stop(pool));
  doca_check(doca_buf_pool_destroy(pool));

  doca_check(doca_mmap_stop(mmap));
  doca_check(doca_mmap_destroy(mmap));

  doca_check(doca_dev_close(dev));

  return 0;
}

void server_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states next_state) {
  switch (next_state) {
    case DOCA_CTX_STATE_IDLE: {
      s_stop = true;
    } break;
    case DOCA_CTX_STATE_STARTING: {
    } break;
    case DOCA_CTX_STATE_RUNNING: {
      s_running = true;
    } break;
    case DOCA_CTX_STATE_STOPPING: {
    } break;
  }
}

void connect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *connection,
                      uint8_t success) {
  assert(success);
  conn = connection;
}

void disconnect_event_cb(doca_comch_event_connection_status_changed *, doca_comch_connection *connection,
                         uint8_t success) {
  assert(success);
  assert(conn == connection);
}

void new_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *connection, uint32_t id) {
  assert(conn == connection);
  remote_consumer_id = id;
}

void expired_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *connection, uint32_t id) {
  assert(conn == connection);
  assert(remote_consumer_id == id);
}

void task_completion_cb(doca_comch_task_send *task, doca_data, doca_data) {
  doca_task_free(doca_comch_task_send_as_task(task));
}

void task_error_cb(doca_comch_task_send *task, doca_data, doca_data) {
  doca_task_free(doca_comch_task_send_as_task(task));
}

void recv_event_cb(struct doca_comch_event_msg_recv *, uint8_t *, uint32_t, struct doca_comch_connection *) {}

void post_send_cb(struct doca_comch_producer_task_send *task, union doca_data, union doca_data) {
  doca_task_free(doca_comch_producer_task_send_as_task(task));
}

void post_send_err_cb(struct doca_comch_producer_task_send *task, union doca_data, union doca_data) {
  doca_task_free(doca_comch_producer_task_send_as_task(task));
}

void post_recv_cb(struct doca_comch_consumer_task_post_recv *task, union doca_data, union doca_data) {
  doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
}

void post_recv_err_cb(struct doca_comch_consumer_task_post_recv *task, union doca_data, union doca_data) {
  doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
}

void producer_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states next_state) {
  switch (next_state) {
    case DOCA_CTX_STATE_IDLE: {
      pro_stop = true;
    } break;
    case DOCA_CTX_STATE_STARTING: {
    } break;
    case DOCA_CTX_STATE_RUNNING: {
      pro_running = true;
    } break;
    case DOCA_CTX_STATE_STOPPING: {
    } break;
  }
}

void consumer_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states next_state) {
  switch (next_state) {
    case DOCA_CTX_STATE_IDLE: {
      con_stop = true;
    } break;
    case DOCA_CTX_STATE_STARTING: {
    } break;
    case DOCA_CTX_STATE_RUNNING: {
      con_running = true;
    } break;
    case DOCA_CTX_STATE_STOPPING: {
    } break;
  }
}