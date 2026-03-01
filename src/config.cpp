#include "config.hpp"
#include <cstdlib>

AgentConfig config_init() {
    AgentConfig config;
    
    // Default values
    config.workspace = ".";
    config.model = "gpt-4o";
    config.base_url = "https://api.openai.com/v1";
    config.debug_mode = false;

    // Environment variables
    if (const char* env_model = std::getenv("AGENT_MODEL")) {
        config.model = env_model;
    }
    if (const char* env_api_key = std::getenv("AGENT_API_KEY")) {
        config.api_key = env_api_key;
    }
    if (const char* env_base_url = std::getenv("AGENT_BASE_URL")) {
        config.base_url = env_base_url;
    }
    if (const char* env_workspace = std::getenv("AGENT_WORKSPACE")) {
        config.workspace = env_workspace;
    }
    if (const char* env_debug = std::getenv("AGENT_DEBUG")) {
        config.debug_mode = (std::string(env_debug) == "true" || std::string(env_debug) == "1");
    }

    return config;
}
