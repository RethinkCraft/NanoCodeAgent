#include "agent_tools.hpp"
#include "bash_tool.hpp"
#include "read_file.hpp"
#include "write_file.hpp"
#include <iostream>

std::string format_tool_error(const std::string& error_msg) {
    nlohmann::json err = {
        {"error", error_msg},
        {"status", "failed"}
    };
    return err.dump();
}

std::string execute_tool(const ToolCall& cmd, const AgentConfig& config) {
    try {
        if (cmd.name == "bash_execute_safe") {
            std::string command = cmd.arguments.value("command", "");
            if (command.empty()) return format_tool_error("Missing 'command' argument for bash_execute_safe.");
            
            // Timeouts could be passed, but default to safe defaults per tool
            int timeout = cmd.arguments.value("timeout_ms", 10000);
            
            auto res = bash_execute_safe(config.workspace_abs, command, timeout, config.max_tool_output_bytes, config.max_tool_output_bytes);
            nlohmann::json out = {
                {"ok", res.ok},
                {"exit_code", res.exit_code},
                {"stdout", res.out_tail},
                {"stderr", res.err_tail},
                {"truncated", res.truncated},
                {"timed_out", res.timed_out},
                {"error", res.err}
            };
            return out.dump();
            
        } else if (cmd.name == "read_file_safe") {
            std::string path = cmd.arguments.value("path", "");
            if (path.empty()) return format_tool_error("Missing 'path' argument for read_file_safe.");
            
            auto res = read_file_safe(config.workspace_abs, path, config.max_tool_output_bytes);
            nlohmann::json out = {
                {"ok", res.ok},
                {"content", res.content},
                {"truncated", res.truncated},
                {"error", res.err}
            };
            return out.dump();
            
        } else if (cmd.name == "write_file_safe") {
            std::string path = cmd.arguments.value("path", "");
            std::string content = cmd.arguments.value("content", "");
            if (path.empty()) return format_tool_error("Missing 'path' argument for write_file_safe.");
            
            auto res = write_file_safe(config.workspace_abs, path, content);
            nlohmann::json out = {
                {"ok", res.ok},
                {"error", res.err}
            };
            return out.dump();
        } else {
            return format_tool_error("Tool '" + cmd.name + "' is not registered.");
        }
    } catch (const std::exception& e) {
        return format_tool_error(std::string("Exception during tool execution: ") + e.what());
    }
}
