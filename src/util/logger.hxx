#pragma once

#include <spdlog/spdlog.h>

#define LOG_CRITI(...) SPDLOG_CRITICAL(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
