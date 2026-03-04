#include "agent_loop.hpp"
#include "agent_utils.hpp"
#include "agent_tools.hpp"
#include "tool_call.hpp"
#include "logger.hpp"
#include <iostream>

void agent_run(const AgentConfig& config, 
               const std::string& system_prompt, 
               const std::string& user_prompt, 
               const nlohmann::json& tools_registry,
               LLMStreamFunc llm_func) {
    
    nlohmann::json messages = nlohmann::json::array();
    
    if (!system_prompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", system_prompt}});
    }
    messages.push_back({{"role", "user"}, {"content", user_prompt}});
    
    int turns = 0;
    int total_tool_calls = 0;
    
    while (true) {
        turns++;
        if (turns > config.max_turns) {
            LOG_ERROR("Broker loop hit max_turns (" + std::to_string(config.max_turns) + "), aborting to prevent infinite loop.");
            std::cerr << "[Agent Error] Exceeded max standard turns.\n";
            break;
        }
        
        enforce_context_limits(messages, config.max_context_bytes);
        
        LOG_INFO("Agent Turn " + std::to_string(turns) + " started...");
        
        // Execute LLM Step
        nlohmann::json response_message;
        try {
            response_message = llm_func(config, messages, tools_registry);
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("LLM Execution failed: ") + e.what());
            std::cerr << "[Agent Error] LLM request failed: " << e.what() << "\n";
            break;
        }
        
        messages.push_back(response_message);
        
        if (!response_message.contains("tool_calls") || response_message["tool_calls"].empty()) {
            LOG_INFO("No tool calls. Agent loop complete.");
            std::cout << "[Agent Complete] " << response_message.value("content", "") << "\n";
            break;
        }
        
        auto tool_calls = response_message["tool_calls"];
        if (tool_calls.size() > config.max_tool_calls_per_turn) {
            LOG_ERROR("Too many tools requested in turn: " + std::to_string(tool_calls.size()));
            std::cerr << "[Agent Error] Tool flood detected, limit " << config.max_tool_calls_per_turn << ". Aborting.\n";
            break;
        }
        
        total_tool_calls += tool_calls.size();
        if (total_tool_calls > config.max_total_tool_calls) {
            LOG_ERROR("Max total tool calls exceeded: " + std::to_string(total_tool_calls));
            std::cerr << "[Agent Error] Global tool call limit hit, aborting.\n";
            break;
        }

        bool state_contaminated = false;
        
        // Ensure sequentially execute tools
        for (const auto& tc_json : tool_calls) {
            ToolCall tc;
            tc.id = tc_json.value("id", "");
            if (tc_json.contains("function")) {
                tc.name = tc_json["function"].value("name", "");
                auto raw_args = tc_json["function"].value("arguments", "{}");
                try {
                    tc.arguments = nlohmann::json::parse(raw_args);
                } catch (...) {
                    LOG_ERROR("Tool JSON parse failed for " + tc.name);
                    tc.arguments = nlohmann::json::object(); // Continue to fail fast via dispatch
                }
            }
            
            std::string output = execute_tool(tc, config);
            
            // Output length guard
            output = truncate_tool_output(output, config.max_tool_output_bytes);
            
            messages.push_back({
                {"role", "tool"},
                {"tool_call_id", tc.id},
                {"name", tc.name},
                {"content", output}
            });
            
            // Check for fail-fast
            if (output.find(R"("status":"failed")") != std::string::npos ||
                output.find(R"("ok":false)") != std::string::npos ||
                output.find(R"("timed_out":true)") != std::string::npos) {
                LOG_ERROR("Tool failed, failing fast: " + output);
                std::cerr << "[Agent Error] Tool " << tc.name << " failed: " << output << "\n";
                state_contaminated = true;
                break;
            }
        }
        
        if (state_contaminated) {
            std::cerr << "[Agent Error] Run stopped due to state contamination (tool failure or timeout).\n";
            break;
        }
    }
}
