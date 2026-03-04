#pragma once

#include "tool_call.hpp"
#include "config.hpp"
#include <string>
#include <nlohmann/json.hpp>

// Dispatches a parsed tool call to its respective system function.
// Returns the tool's textual result, or a structured error message if it fails.
std::string execute_tool(const ToolCall& cmd, const AgentConfig& config);

// Returns the JSON schema representing the tools the agent is capable of running
nlohmann::json get_agent_tools_schema();

// A simple utility to get a generic JSON error
std::string format_tool_error(const std::string& error_msg);
