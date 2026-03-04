#include "cli.hpp"
#include <iostream>
#include <getopt.h>
#include <cstdlib>

void print_help() {
    std::cout << "Usage: agent [OPTIONS] -e <prompt>\n\n"
              << "A C++23 based coding agent.\n\n"
              << "Options:\n"
              << "  -e, --execute <prompt>   The task or prompt for the agent to execute (Required)\n"
              << "  -w, --workspace <dir>    Working directory for the agent (Default: .)\n"
              << "  --config <path>          Path to configuration file (Env: NCA_CONFIG)\n"
              << "  --model <name>           LLM model to use (Default: gpt-4o, Env: NCA_MODEL)\n"
              << "  --api-key <key>          API key for the model provider (Env: NCA_API_KEY)\n"
              << "  --base-url <url>         Base URL for the API (Env: NCA_BASE_URL)\n"
              << "  --debug                  Enable verbose debug logging (Env: NCA_DEBUG)\n"
              << "  -h, --help               Show this help message and exit\n"
              << "  -v, --version            Show version information and exit\n\n"
              << "Example:\n"
              << "  agent -w /tmp/project -e \"Create a Python script\" --debug\n";
}

void print_version() {
    std::cout << "nano-agent version 0.1.0\n";
}

CliResult cli_parse(int argc, char* argv[], AgentConfig& config) {
    const char* const short_opts = "he:w:v";
    const option long_opts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, 'v'},
        {"execute", required_argument, nullptr, 'e'},
        {"workspace", required_argument, nullptr, 'w'},
        {"config", required_argument, nullptr, 1004},
        {"model", required_argument, nullptr, 1000},
        {"api-key", required_argument, nullptr, 1001},
        {"base-url", required_argument, nullptr, 1002},
        {"debug", no_argument, nullptr, 1003},
        {"max-turns", required_argument, nullptr, 2000},
        {"max-tool-calls-per-turn", required_argument, nullptr, 2001},
        {"max-total-tool-calls", required_argument, nullptr, 2002},
        {"max-tool-output-bytes", required_argument, nullptr, 2003},
        {"max-context-bytes", required_argument, nullptr, 2004},
        {nullptr, no_argument, nullptr, 0}
    };

    // Ensure getopt_long state is completely reset to allow multiple parsing in tests
    optind = 1;

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                print_help();
                return CliResult::ExitSuccess;
            case 'v':
                print_version();
                return CliResult::ExitSuccess;
            case 'e':
                config.prompt = optarg;
                break;
            case 'w':
                config.workspace = optarg;
                break;
            case 1004:
                config.config_file_path = optarg;
                break;
            case 1000:
                config.model = optarg;
                break;
            case 1001:
                config.api_key = optarg;
                break;
            case 1002:
                config.base_url = optarg;
                break;
            case 1003:
                config.debug_mode = true;
                break;
            case 2000:
                config.max_turns = std::stoi(optarg);
                break;
            case 2001:
                config.max_tool_calls_per_turn = std::stoi(optarg);
                break;
            case 2002:
                config.max_total_tool_calls = std::stoi(optarg);
                break;
            case 2003:
                config.max_tool_output_bytes = std::stoull(optarg);
                break;
            case 2004:
                config.max_context_bytes = std::stoull(optarg);
                break;
            case '?':
            default:
                std::cerr << "Try 'agent --help' for more information.\n";
                return CliResult::ExitFailure;
        }
    }

    if (config.prompt.empty()) {
        std::cerr << "Error: Missing required argument '-e' or '--execute'.\n";
        std::cerr << "Try 'agent --help' for more information.\n";
        return CliResult::ExitFailure;
    }

    return CliResult::Success;
}
