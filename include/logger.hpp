#pragma once

#include <spdlog/spdlog.h>

void logger_init(bool debug_mode);

// Macros for logging
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
