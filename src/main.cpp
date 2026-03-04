#include "cli.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "agent_loop.hpp"
#include "agent_tools.hpp"
#include "llm.hpp"
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <filesystem>

static std::string load_system_prompt(const AgentConfig& config) {
    if (config.system_prompt_file.has_value()) {
        std::ifstream f(config.system_prompt_file.value());
        if (f.is_open()) {
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return content;
        } else {
            LOG_ERROR("Could not read system prompt file: {}", config.system_prompt_file.value());
            std::exit(EXIT_FAILURE);
        }
    }
    
    return "You are an autonomous AI programming agent. "
           "You have access to tools that let you read/write files and execute bash commands. "
           "You MUST ONLY operate within the workspace directory. "
           "Forbidden strictly: Any destructive commands outside the workspace or dangerous rm -rf commands. "
           "When you need to act, use the provided tools according to their schema. Focus on the user's task and exit when done.";
}

int main(int argc, char* argv[]) {
    // 1. Initialize configuration & Parse CLI arguments
    AgentConfig config = config_init(argc, argv);
    CliResult cli_res = cli_parse(argc, argv, config);
    if (cli_res == CliResult::ExitSuccess) return EXIT_SUCCESS;
    if (cli_res == CliResult::ExitFailure) return EXIT_FAILURE;

    // 2. Initialize logger
    logger_init(config.debug_mode);

    // 3. Workspace setup & sanity checks
    std::filesystem::path ws(config.workspace);
    if (!std::filesystem::exists(ws)) {
        LOG_INFO("Workspace directory does not exist. Creating it: {}", config.workspace);
        std::filesystem::create_directories(ws);
    }
    config.workspace_abs = std::filesystem::canonical(ws).string();
    LOG_DEBUG("Absolute workspace path: {}", config.workspace_abs);

    if (config.mode == "real" && (!config.api_key.has_value() || config.api_key->empty())) {
        LOG_ERROR("API key is missing! Provide --api-key or set NCA_API_KEY environment variable.");
        return EXIT_FAILURE;
    }

    // 4. Set up the foundational Agent Loop objects
    std::string sys_prompt = load_system_prompt(config);
    nlohmann::json tools = get_agent_tools_schema();

    if (config.dry_run) {
        std::cout << "--- DRY RUN MODE ---\n";
        std::cout << "System Prompt: \n" << sys_prompt << "\n\n";
        std::cout << "User Prompt: \n" << config.prompt << "\n\n";
        std::cout << "Tools Schema: \n" << tools.dump(2) << "\n";
        return EXIT_SUCCESS;
    }

    LLMStreamFunc llm_func;

    if (config.mode == "mock") {
        if (!config.mock_fixture.has_value()) {
            LOG_ERROR("Mock mode requires --mock-fixture <path>");
            return EXIT_FAILURE;
        }
        
        LOG_INFO("Starting in MOCK mode using fixture: {}", config.mock_fixture.value());
        // Simple mock mechanism: reads the json file and parses an array of tool call responses
        
        std::ifstream mf(config.mock_fixture.value());
        if (!mf.is_open()) {
            LOG_ERROR("Could not open mock fixture file.");
            return EXIT_FAILURE;
        }
        nlohmann::json fixture_data;
        try {
            mf >> fixture_data;
        } catch (const std::exception& e) {
            LOG_ERROR("Invalid json in mock fixture: {}", e.what());
            return EXIT_FAILURE;
        }
        
        // Use a shared pointer to make the lambda copyable/copy-assignable if it captures mutable state, 
        // wait, we can just use std::shared_ptr or mutable lambda. Since LLMStreamFunc is std::function, 
        // a mutable lambda works but only if it's copyable.
        auto shared_step = std::make_shared<int>(0);
        llm_func = [fixture_data, shared_step](const AgentConfig&, const nlohmann::json& msgs, const nlohmann::json&) -> nlohmann::json {
            if (*shared_step >= fixture_data.size()) {
                return nlohmann::json{
                    {"role", "assistant"},
                    {"content", "Mock fixture steps exhausted."}
                };
            }
            nlohmann::json ret = fixture_data[*shared_step];
            (*shared_step)++;
            return ret;
        };

    } else {
        LOG_INFO("Starting in REAL network mode...");
        llm_func = [](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json& t) -> nlohmann::json {
            auto on_delta = [](const std::string& txt) -> bool {
                std::cout << txt << std::flush;
                return true;
            };
            
            std::cout << "\n\033[36m[Agent Thinking...]\033[0m\n";
            auto response = llm_chat_completion_stream(cfg, msgs, t, on_delta);
            std::cout << "\n"; // Newline after stream finishes
            
            return response;
        };
    }

    // 5. Run the Agent Loop
    try {
        agent_run(config, sys_prompt, config.prompt, tools, llm_func);
        LOG_INFO("Agent loop finished successfully.");
    } catch (const std::exception& e) {
        LOG_ERROR("Agent loop terminated with error: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
