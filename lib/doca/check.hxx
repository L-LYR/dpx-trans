#pragma once

#include <doca_error.h>

#include <algorithm>
#include <ranges>
#include <span>

#include "util/fatal.hxx"

#ifdef ENABLE_CHECK_FOOTPRINT

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

#else

#define doca_check_ext(expr, ...)                                                                              \
  do {                                                                                                         \
    constexpr doca_error_t __expected_result__[] = {DOCA_SUCCESS __VA_OPT__(, ) __VA_ARGS__};                  \
    constexpr size_t __n_expected__ = sizeof(__expected_result__) / sizeof(doca_error_t);                      \
    doca_error_t __doca_check_result__ = expr;                                                                 \
    if (std::ranges::none_of(std::span<const doca_error_t>(__expected_result__, __n_expected__),               \
                             [&__doca_check_result__](doca_error_t e) { return e == __doca_check_result__; })) \
        [[unlikely]] {                                                                                         \
      die(#expr ": {}", doca_error_get_descr(__doca_check_result__));                                          \
    }                                                                                                          \
  } while (0);

#define doca_check(expr)                                              \
  do {                                                                \
    doca_error_t __doca_check_result__ = expr;                        \
    if (__doca_check_result__ != DOCA_SUCCESS) [[unlikely]] {         \
      die(#expr ": {}", doca_error_get_descr(__doca_check_result__)); \
    }                                                                 \
  } while (0);

#endif
