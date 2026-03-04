#pragma once

#include <string>
#include <optional>

struct AgentConfig {
    std::string prompt;
    std::string workspace;
    std::string workspace_abs; // Normalized absolute path to workspace
    std::string model;
    std::optional<std::string> api_key;
    std::string base_url;
    bool debug_mode;
    std::string config_file_path; // Path to loaded config file, if any
    
    // Agent limits / Brake system
    int max_turns = 20;
    int max_tool_calls_per_turn = 8;
    int max_total_tool_calls = 50;
    size_t max_tool_output_bytes = 16384; // 16KB
    size_t max_context_bytes = 204800;    // 200KB
};

// Initialize configuration considering Defaults < Config File < ENV
AgentConfig config_init(int argc, char* argv[]);
