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
            
            // Safe parsing of timeout_ms to tolerate strings (LLMs sometimes pass "2000" instead of 2000)
            int timeout = 5000;
            if (cmd.arguments.contains("timeout_ms")) {
                auto& t = cmd.arguments["timeout_ms"];
                if (t.is_number()) timeout = t.get<int>();
                else if (t.is_string()) {
                    try { timeout = std::stoi(t.get<std::string>()); } catch(...) {}
                }
            }
            
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

nlohmann::json get_agent_tools_schema() {
    return nlohmann::json::array({
        {
            {"type", "function"},
            {"function", {
                {"name", "read_file_safe"},
                {"description", "Reads the complete contents of a given file."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {
                            {"type", "string"},
                            {"description", "The absolute or relative path to the file to read."}
                        }}
                    }},
                    {"required", {"path"}}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "write_file_safe"},
                {"description", "Writes string content to a file. Any existing file is overwritten."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"path", {
                            {"type", "string"},
                            {"description", "The absolute or relative path to the file to write."}
                        }},
                        {"content", {
                            {"type", "string"},
                            {"description", "The text content to be written."}
                        }}
                    }},
                    {"required", {"path", "content"}}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "bash_execute_safe"},
                {"description", "Executes a bash shell command and returns the stdout, stderr, and exit code."},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"command", {
                            {"type", "string"},
                            {"description", "The bash command to run (e.g. `ls -la`, `mkdir test`)."}
                        }},
                        {"timeout_ms", {
                            {"type", "integer"},
                            {"description", "Timeout in milliseconds for the command. Default is 5000."}
                        }}
                    }},
                    {"required", {"command"}}
                }}
            }}
        }
    });
}
