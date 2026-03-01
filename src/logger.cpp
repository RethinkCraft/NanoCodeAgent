#include "logger.hpp"

void logger_init(bool debug_mode) {
    // Set global log pattern
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Set log level based on debug mode
    if (debug_mode) {
        spdlog::set_level(spdlog::level::debug);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
}
