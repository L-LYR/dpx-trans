#if 0
/usr/bin/clang++-18 -Wall -Wextra -pedantic -std=c++20 $(pkg-config --libs doca-common doca-comch) $(pkg-config --cflags doca-common doca-comch) "$0"; if [ $? -eq 0 ]; then ./a.out "$@"; rm ./a.out; fi; exit
#endif

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_buf_pool.h>
#include <doca_comch.h>
#include <doca_comch_consumer.h>
#include <doca_comch_producer.h>
#include <doca_ctx.h>
#include <doca_dev.h>
#include <doca_mmap.h>
#include <doca_pe.h>

#include <algorithm>
#include <format>
#include <iostream>
#include <source_location>
#include <thread>

namespace std {
inline string to_string(const source_location &l) {
  return format("{}:{} `{}`: ", l.file_name(), l.line(), l.function_name());
}
}  // namespace std

[[noreturn]] inline void die(std::string why) { throw std::runtime_error(why); }
inline void footprint(std::string what) { std::cerr << what << std::endl; }

#define die(fmt, ...) \
  die(std::to_string(std::source_location::current()) + std::vformat(fmt, std::make_format_args(__VA_ARGS__)))

#define footprint(fmt, ...) \
  footprint(std::to_string(std::source_location::current()) + std::vformat(fmt, std::make_format_args(__VA_ARGS__)))

#define doca_check_ext(expr, ...)                                                                              \
  do {                                                                                                         \
    constexpr doca_error_t __expected_result__[] = {DOCA_SUCCESS __VA_OPT__(, ) __VA_ARGS__};                  \
    constexpr size_t __n_expected__ = sizeof(__expected_result__) / sizeof(doca_error_t);                      \
    doca_error_t __doca_check_result__ = expr;                                                                 \
    if (std::ranges::none_of(std::span<const doca_error_t>(__expected_result__, __n_expected__),               \
                             [&__doca_check_result__](doca_error_t e) { return e == __doca_check_result__; })) \
        [[unlikely]] {                                                                                         \
      die(#expr ": {}", doca_error_get_descr(__doca_check_result__));                                          \
    } else {                                                                                                   \
      footprint(#expr ": {}", doca_error_get_descr(__doca_check_result__));                                    \
    }                                                                                                          \
  } while (0);

#define doca_check(expr)                                                    \
  do {                                                                      \
    doca_error_t __doca_check_result__ = expr;                              \
    if (__doca_check_result__ != DOCA_SUCCESS) [[unlikely]] {               \
      die(#expr ": {}", doca_error_get_descr(__doca_check_result__));       \
    } else {                                                                \
      footprint(#expr ": {}", doca_error_get_descr(__doca_check_result__)); \
    }                                                                       \
  } while (0);

using namespace std::chrono_literals;

std::string pci_addr = "0000:99:00.0";
doca_dev *dev = nullptr;
uint32_t n_devs = 0;
doca_devinfo **dev_list;

size_t len = 8192;
size_t piece_len = 4096;
uint8_t buffer[8192] = {};
doca_mmap *mmap = nullptr;

doca_buf_pool *pool = nullptr;

doca_buf *send_buf = nullptr;
bool send = false;

doca_buf *recv_buf = nullptr;
bool recv = false;

doca_pe *pe = nullptr;

std::string name = "debug";
doca_comch_client *c;
doca_comch_connection *conn = nullptr;

uint32_t max_msg_size = 0;
uint32_t recv_queue_size = 0;

template <typename fn>
void poll_until(fn &&predictor) {
  while (!predictor()) {
    if (!doca_pe_progress(pe)) {
      std::this_thread::sleep_for(10us);
    }
  }
};

bool c_running = false;
bool c_stop = false;

doca_comch_producer *pro = nullptr;
bool pro_running = false;
bool pro_stop = false;

doca_comch_consumer *con = nullptr;
bool con_running = false;
bool con_stop = false;

uint32_t remote_consumer_id = 0;

void server_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);
void new_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);
void expired_consumer_event_cb(doca_comch_event_consumer *, doca_comch_connection *, uint32_t);
void task_completion_cb(doca_comch_task_send *, doca_data, doca_data);
void task_error_cb(doca_comch_task_send *, doca_data, doca_data);
void recv_event_cb(doca_comch_event_msg_recv *, uint8_t *, uint32_t, doca_comch_connection *);
void producer_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);
void post_send_cb(doca_comch_producer_task_send *, doca_data, doca_data);
void post_send_err_cb(doca_comch_producer_task_send *, doca_data, doca_data);
void consumer_state_change_cb(const doca_data, doca_ctx *, doca_ctx_states, doca_ctx_states);
void post_recv_cb(doca_comch_consumer_task_post_recv *, doca_data, doca_data);
void post_recv_err_cb(doca_comch_consumer_task_post_recv *, doca_data, doca_data);

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

  doca_check(doca_mmap_create(&mmap));
  doca_check(doca_mmap_add_dev(mmap, dev));
  doca_check(doca_mmap_set_permissions(mmap, DOCA_ACCESS_FLAG_PCI_READ_WRITE));
  doca_check(doca_mmap_set_memrange(mmap, buffer, len));
  doca_check(doca_mmap_start(mmap));

  doca_check(doca_buf_pool_create(len / piece_len, piece_len, mmap, &pool));
  doca_check(doca_buf_pool_start(pool));

  doca_check(doca_buf_pool_buf_alloc(pool, &send_buf));
  memset(buffer, 'A', piece_len);
  doca_check(doca_buf_set_data_len(send_buf, piece_len / 2));

  memset(&buffer[piece_len], 'B', piece_len);
  doca_check(doca_buf_pool_buf_alloc(pool, &recv_buf));

  doca_check(doca_pe_create(&pe));

  doca_check(doca_comch_cap_get_max_msg_size(doca_dev_as_devinfo(dev), &max_msg_size));
  doca_check(doca_comch_cap_get_max_recv_queue_size(doca_dev_as_devinfo(dev), &recv_queue_size));

  doca_check(doca_comch_client_create(dev, name.data(), &c));
  auto c_ctx = doca_comch_client_as_ctx(c);
  doca_check(doca_comch_client_task_send_set_conf(c, task_completion_cb, task_error_cb, recv_queue_size));
  doca_check(doca_comch_client_event_msg_recv_register(c, recv_event_cb));
  doca_check(doca_comch_client_event_consumer_register(c, new_consumer_event_cb, expired_consumer_event_cb));
  doca_check(doca_comch_client_set_max_msg_size(c, max_msg_size));
  doca_check(doca_comch_client_set_recv_queue_size(c, recv_queue_size));
  doca_check(doca_pe_connect_ctx(pe, c_ctx));
  doca_check(doca_ctx_set_state_changed_cb(c_ctx, server_state_change_cb));
  doca_check_ext(doca_ctx_start(c_ctx), DOCA_ERROR_IN_PROGRESS);

  poll_until([]() { return c_running; });
  doca_check(doca_comch_client_get_connection(c, &conn));

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

  std::this_thread::sleep_for(5s);

  {
    doca_comch_consumer_task_post_recv *task;
    doca_check(doca_comch_consumer_task_post_recv_alloc_init(con, recv_buf, &task));
    doca_check(doca_task_submit(doca_comch_consumer_task_post_recv_as_task(task)));
  }
  {
    doca_comch_producer_task_send *task;
    doca_check(doca_comch_producer_task_send_alloc_init(pro, send_buf, nullptr, 0, remote_consumer_id, &task));
    doca_check(doca_task_submit(doca_comch_producer_task_send_as_task(task)));
  }
  poll_until([]() { return send; });
  poll_until([]() { return recv; });

  doca_check(doca_ctx_stop(pro_ctx));
  poll_until([]() { return pro_stop; });
  doca_check(doca_comch_producer_destroy(pro));

  doca_check(doca_ctx_stop(con_ctx));
  poll_until([]() { return con_stop; });
  doca_check(doca_comch_consumer_destroy(con));

  doca_check_ext(doca_ctx_stop(c_ctx), DOCA_ERROR_IN_PROGRESS);
  poll_until([]() { return c_stop; });

  doca_check(doca_comch_client_destroy(c));

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
      c_stop = true;
    } break;
    case DOCA_CTX_STATE_STARTING: {
    } break;
    case DOCA_CTX_STATE_RUNNING: {
      c_running = true;
    } break;
    case DOCA_CTX_STATE_STOPPING: {
    } break;
  }
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

void recv_event_cb(doca_comch_event_msg_recv *, uint8_t *, uint32_t, doca_comch_connection *) {}

void post_send_cb(doca_comch_producer_task_send *task, doca_data, doca_data) {
  send = true;
  doca_task_free(doca_comch_producer_task_send_as_task(task));
}

void post_send_err_cb(doca_comch_producer_task_send *task, doca_data, doca_data) {
  send = true;
  doca_task_free(doca_comch_producer_task_send_as_task(task));
}

void post_recv_cb(doca_comch_consumer_task_post_recv *task, doca_data, doca_data) {
  recv = true;
  auto buf = doca_comch_consumer_task_post_recv_get_buf(task);
  void *data = nullptr;
  doca_check(doca_buf_get_data(buf, &data));
  std::cout << (char *)data << std::endl;
  doca_task_free(doca_comch_consumer_task_post_recv_as_task(task));
}

void post_recv_err_cb(doca_comch_consumer_task_post_recv *task, doca_data, doca_data) {
  recv = true;
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
