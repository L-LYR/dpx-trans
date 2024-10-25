#pragma once

#include <doca_error.h>

#include "util/fatal.hxx"

#define doca_check(expr)                                              \
  do {                                                                \
    doca_error_t __doca_check_result__ = expr;                        \
    if (__doca_check_result__ != DOCA_SUCCESS) {                      \
      die(#expr ": {}", doca_error_get_descr(__doca_check_result__)); \
    }                                                                 \
  } while (0);
