#include "config.hpp"
#include "cli.hpp"
#include "logger.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Initialize configuration with defaults and environment variables
    AgentConfig config = config_init();

    // 2. Parse command line arguments
    if (!cli_parse(argc, argv, config)) {
        return 0; // Exit normally if help/version was requested or error occurred
    }

    // 3. Initialize logger
    logger_init(config.debug_mode);

    // 4. Log configuration details (Debug level)
    LOG_DEBUG("Configuration loaded:");
    LOG_DEBUG("  Workspace: {}", config.workspace);
    LOG_DEBUG("  Model: {}", config.model);
    LOG_DEBUG("  Base URL: {}", config.base_url);
    LOG_DEBUG("  API Key: {}", config.api_key.has_value() ? "***" : "Not set");
    LOG_DEBUG("  Prompt: {}", config.prompt);

    // 5. Stub for execution
    LOG_INFO("准备调用模型处理任务: {}", config.prompt);

    return 0;
}
