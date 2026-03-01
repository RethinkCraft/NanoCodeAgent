#pragma once

#include <string>
#include <optional>

struct AgentConfig {
    std::string prompt;
    std::string workspace;
    std::string model;
    std::optional<std::string> api_key;
    std::string base_url;
    bool debug_mode;
};

// Initialize configuration with default values and environment variables
AgentConfig config_init();
