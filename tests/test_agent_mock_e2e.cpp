#include <gtest/gtest.h>
#include "agent_tools.hpp"
#include "agent_loop.hpp"
#include "config.hpp"
#include "repo_tools.hpp"
#include "workspace.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

namespace {

std::string shell_escape_single_quotes(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

int run_bash(const std::string& command) {
    const std::string wrapped = "bash -lc '" + shell_escape_single_quotes(command) + "'";
    return std::system(wrapped.c_str());
}

std::string tool_message_content_for_id(const nlohmann::json& messages, const std::string& tool_call_id) {
    for (const auto& message : messages) {
        if (message.value("role", "") != "tool") {
            continue;
        }
        if (message.value("tool_call_id", "") == tool_call_id) {
            return message.value("content", "");
        }
    }
    return "";
}

} // namespace

class AgentMockE2ETest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        std::random_device rd;
        test_workspace = (std::filesystem::temp_directory_path() / ("nano_e2e_" + std::to_string(rd()) + std::to_string(rd()))).string();
        std::filesystem::create_directories(test_workspace);
    }

    void TearDown() override {
        clear_rg_binary_for_testing();
        std::filesystem::remove_all(test_workspace);
    }

    void create_file(const std::string& rel_path, const std::string& content) {
        const auto path = std::filesystem::path(test_workspace) / rel_path;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    std::map<std::string, std::string> snapshot_workspace_files() const {
        std::map<std::string, std::string> snapshot;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(test_workspace)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            std::ifstream in(entry.path(), std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)), {});
            snapshot[std::filesystem::relative(entry.path(), test_workspace).generic_string()] = content;
        }
        return snapshot;
    }

    std::string create_fake_rg_script() {
        const auto path = std::filesystem::path(test_workspace) / "fake-rg.sh";
        std::ofstream out(path);
        out << "#!/bin/sh\n";
        out << "query=\"$5\"\n";
        out << "target=\"$6\"\n";
        out << "if [ \"$query\" = \"NanoSymbol\" ]; then\n";
        out << "  grep -R -n -- \"$query\" \"$target\" | while IFS=: read -r file line rest; do\n";
        out << "    printf '{\"type\":\"match\",\"data\":{\"path\":{\"text\":\"%s\"},\"line_number\":%s,\"submatches\":[{\"start\":0}],\"lines\":{\"text\":\"match:%s\\\\n\"}}}\\n' \"$file\" \"$line\" \"$query\"\n";
        out << "  done\n";
        out << "  exit 0\n";
        out << "fi\n";
        out << "exit 1\n";
        out.close();
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                     std::filesystem::perms::group_exec | std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_exec | std::filesystem::perms::others_read);
        return path.string();
    }
};

TEST_F(AgentMockE2ETest, MockFullChain) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 50;
    config.max_tool_output_bytes = 1024;
    config.max_context_bytes = 10000;
    
    int turn = 0;
    
    LLMStreamFunc mock_llm = [&](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) { // 1. Write file
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "call_write"},
                        {"function", {
                            {"name", "write_file_safe"},
                            {"arguments", "{\"path\":\"hello.txt\",\"content\":\"world\"}"}
                        }}
                    }
                }}
            };
        } else if (turn == 2) { // 2. Bash execute safe
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "call_bash"},
                        {"function", {
                            {"name", "bash_execute_safe"},
                            {"arguments", "{\"command\":\"cat hello.txt\",\"timeout_ms\":2000}"}
                        }}
                    }
                }}
            };
        } else if (turn == 3) { // 3. Read file safe
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "call_read"},
                        {"function", {
                            {"name", "read_file_safe"},
                            {"arguments", "{\"path\":\"hello.txt\"}"}
                        }}
                    }
                }}
            };
        } else { // 4. Final message
            return nlohmann::json{
                {"role", "assistant"},
                {"content", "Task completed"}
            };
        }
    };
    
    agent_run(config, "sys", "DO IT", nlohmann::json::array(), mock_llm);
    
    // Validate final effect
    EXPECT_EQ(turn, 4); // Executed 4 turns
    
    // Assert the file was created in the test's temporary workspace
    EXPECT_TRUE(std::filesystem::exists(test_workspace + "/hello.txt"));
}

TEST_F(AgentMockE2ETest, MockReadOnlyRepoObservationChainDoesNotWriteWorkspace) {
    ASSERT_EQ(run_bash("cd '" + test_workspace + "' && git init -b main >/dev/null 2>&1"), 0);
    create_file("src/main.cpp", "int NanoSymbol = 7;\n");
    set_rg_binary_for_testing(create_fake_rg_script());

    const auto before_snapshot = snapshot_workspace_files();

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 50;
    config.max_tool_output_bytes = 4096;
    config.max_context_bytes = 20000;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "call_git"},
                    {"function", {
                        {"name", "git_status"},
                        {"arguments", "{}"}
                    }}
                }}}
            };
        }
        if (turn == 2) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "call_rg"},
                    {"function", {
                        {"name", "rg_search"},
                        {"arguments", "{\"query\":\"NanoSymbol\",\"directory\":\"src\",\"max_results\":5}"}
                    }}
                }}}
            };
        }
        if (turn == 3) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "call_read"},
                    {"function", {
                        {"name", "read_file_safe"},
                        {"arguments", "{\"path\":\"src/main.cpp\"}"}
                    }}
                }}}
            };
        }
        return nlohmann::json{
            {"role", "assistant"},
            {"content", "Read-only inspection complete"}
        };
    };

    agent_run(config, "sys", "inspect repo", get_agent_tools_schema(), mock_llm);

    EXPECT_EQ(turn, 4);
    EXPECT_EQ(snapshot_workspace_files(), before_snapshot);
}

TEST_F(AgentMockE2ETest, ParentDelegatesChildAndReceivesStructuredSummary) {
    create_file("delegate.txt", "delegate me");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 4;
    config.max_tool_calls_per_turn = 4;
    config.max_total_tool_calls = 8;
    config.max_tool_output_bytes = 4096;
    config.max_context_bytes = 12000;

    int parent_turn = 0;
    int child_turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();

    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& msgs, const nlohmann::json&) -> nlohmann::json {
        const std::string first_system = msgs[0].value("content", "");
        if (first_system.find("temporary delegated subagent") != std::string::npos) {
            child_turn++;
            if (child_turn == 1) {
                return nlohmann::json{
                    {"role", "assistant"},
                    {"tool_calls", {{
                        {"id", "child_read"},
                        {"function", {
                            {"name", "read_file_safe"},
                            {"arguments", "{\"path\":\"delegate.txt\"}"}
                        }}
                    }}}
                };
            }

            return nlohmann::json{
                {"role", "assistant"},
                {"content",
                 "{\"ok\":true,\"result_summary\":\"Read delegate.txt\",\"files_touched\":[\"delegate.txt\"],"
                 "\"key_facts\":[\"delegate.txt contains delegate me\"],\"open_questions\":[],"
                 "\"commands_ran\":[],\"verification_passed\":true,\"error\":\"\"}"}
            };
        }

        parent_turn++;
        if (parent_turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "delegate_call"},
                    {"function", {
                        {"name", "delegate_subagent"},
                        {"arguments",
                         "{\"role\":\"reviewer\",\"task\":\"Inspect delegate.txt\",\"context_files\":[\"delegate.txt\"],"
                         "\"expected_output\":\"Return the important fact\",\"max_turns\":2}"}
                    }}
                }}}
            };
        }

        second_turn_messages = msgs;
        return nlohmann::json{
            {"role", "assistant"},
            {"content", "Parent integrated the child summary"}
        };
    };

    agent_run(config, "parent system", "delegate a side task", get_agent_tools_schema(), mock_llm);

    ASSERT_EQ(parent_turn, 2);
    ASSERT_EQ(child_turn, 2);

    const auto result = nlohmann::json::parse(tool_message_content_for_id(second_turn_messages, "delegate_call"));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["role"], "reviewer");
    EXPECT_EQ(result["task"], "Inspect delegate.txt");
    EXPECT_EQ(result["result_summary"], "Read delegate.txt");
    EXPECT_EQ(result["files_touched"], nlohmann::json::array({"delegate.txt"}));
    EXPECT_EQ(result["key_facts"], nlohmann::json::array({"delegate.txt contains delegate me"}));
}
