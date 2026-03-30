#include <gtest/gtest.h>
#include "agent_loop.hpp"
#include "agent_utils.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "agent_tools.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

bool message_content_contains(const nlohmann::json& messages,
                              const std::string& role,
                              const std::string& needle) {
    for (const auto& message : messages) {
        if (message.value("role", "") != role) {
            continue;
        }
        if (message.value("content", "").find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

int count_messages_with_role(const nlohmann::json& messages, const std::string& role) {
    int count = 0;
    for (const auto& message : messages) {
        if (message.value("role", "") == role) {
            ++count;
        }
    }
    return count;
}

std::string first_message_content_with_role(const nlohmann::json& messages, const std::string& role) {
    for (const auto& message : messages) {
        if (message.value("role", "") == role) {
            return message.value("content", "");
        }
    }
    return "";
}

std::string nth_message_content_with_role(const nlohmann::json& messages,
                                          const std::string& role,
                                          int ordinal) {
    int seen = 0;
    for (const auto& message : messages) {
        if (message.value("role", "") != role) {
            continue;
        }
        if (seen == ordinal) {
            return message.value("content", "");
        }
        ++seen;
    }
    return "";
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

}  // namespace

class AgentLoopLimitsTest : public ::testing::Test {
protected:
    std::string test_workspace;

    void SetUp() override {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        test_workspace = (std::filesystem::temp_directory_path() / ("nano_limits_" + std::to_string(now))).string();
        std::filesystem::create_directories(test_workspace);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_workspace);
    }

    void write_file(const std::string& rel_path, const std::string& content) {
        const auto path = std::filesystem::path(test_workspace) / rel_path;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    void write_build_script(const std::string& body) {
        const auto path = std::filesystem::path(test_workspace) / "build.sh";
        std::ofstream out(path, std::ios::binary);
        out << "#!/bin/sh\n";
        out << body;
        out.close();
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                std::filesystem::perms::group_exec | std::filesystem::perms::group_read |
                std::filesystem::perms::others_exec | std::filesystem::perms::others_read);
    }
};

TEST_F(AgentLoopLimitsTest, MaxTurnsHalt) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 50;
    config.max_context_bytes = 100000;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // To avoid fail-fast killing the loop, we must return a real tool with valid arguments
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {
                    {"id", "call_" + std::to_string(call_count)},
                    {"type", "function"},
                    {"function", {
                        {"name", "bash_execute_safe"},
                        {"arguments", "{\"command\":\"echo ok\",\"timeout_ms\":1000}"}
                    }}
                }
            }}
        };
    };
    
    agent_run(config, "System", "Prompt", nlohmann::json::array(), mock_llm);
    
    // Should successfully cycle and hit max_turns limit instead of failing fast on turn 1
    EXPECT_EQ(call_count, 3);
}

TEST_F(AgentLoopLimitsTest, MaxToolCallsPerTurn) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 2; // Limit 2
    config.max_total_tool_calls = 50;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // Return 3 tools, which exceeds the per-turn limit
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "1"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "2"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 2\"}"}}}},
                {{"id", "3"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 3\"}"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Fails fast at Turn 1 constraint checking, before executing
    EXPECT_EQ(call_count, 1);
}

TEST_F(AgentLoopLimitsTest, MaxTotalToolCalls) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 7;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // 4 valid tools per turn
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "1"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "2"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "3"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}},
                {{"id", "4"}, {"function", {{"name", "bash_execute_safe"}, {"arguments", "{\"command\":\"echo 1\"}"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Turn 1: +4 tools -> total 4 (Passes check, executes perfectly). Loop continues.
    // Turn 2: +4 tools -> total 8 (> 7). Fails check globally, aborts turn 2 before executing.
    EXPECT_EQ(call_count, 2);
}

TEST_F(AgentLoopLimitsTest, ToolOutputTruncation) {
    std::string huge(20000, 'A');
    std::string out = truncate_tool_output(huge, 100);
    
    std::string expected_marker = "\n<TRUNCATED: original=20000 bytes, kept=100 bytes>";
    
    // Validate exact hard limit sizes combined with marker padding
    EXPECT_EQ(out.size(), 100 + expected_marker.size());
    EXPECT_NE(out.find("<TRUNCATED: original="), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, ContextSlidingWindow) {
    nlohmann::json messages = {
        {{"role", "system"}, {"content", "sys"}},
        {{"role", "user"}, {"content", "req"}},
        {{"role", "tool"}, {"content", std::string(1000, 'B')}}, // 1000 bytes block
        {{"role", "tool"}, {"content", std::string(1000, 'C')}}  // 1000 bytes block
    };
    
    // Apply highly constrained limit (~500 bytes)
    enforce_context_limits(messages, 500);
    
    // Should shrink physical size
    EXPECT_LE(messages.dump().size(), 500);
    
    // Must drop both previous tool histories replacing completely:
    EXPECT_EQ(messages[2]["content"], "<DROPPED old tool outputs to fit context>");
    EXPECT_EQ(messages[3]["content"], "<DROPPED old tool outputs to fit context>");
}

TEST_F(AgentLoopLimitsTest, FailFastOnToolError) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 10;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // Attempt to read a non-existent file, which will fail the tool and should break the loop instantly
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "fail_1"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"does_not_exist_xyz.txt\"}"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Expect the agent to stop immediately after turn 1 due to State Contamination / fail-fast
    EXPECT_EQ(call_count, 1);
}

TEST_F(AgentLoopLimitsTest, ApplyPatchNoMatchContinuesWithGuidance) {
    write_file("retry.txt", "hello world");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "patch_1"},
                    {"function", {
                        {"name", "apply_patch"},
                        {"arguments", "{\"path\":\"retry.txt\",\"old_text\":\"missing\",\"new_text\":\"patched\"}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"reject_code\":\"no_match\""));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "Tool recovery guidance for apply_patch"));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "classification=needs_inspection"));
}

TEST_F(AgentLoopLimitsTest, ApplyPatchMultipleMatchesContinuesWithGuidance) {
    write_file("multi.txt", "dup dup");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "patch_multi"},
                    {"function", {
                        {"name", "apply_patch"},
                        {"arguments", "{\"path\":\"multi.txt\",\"old_text\":\"dup\",\"new_text\":\"DUP\"}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"reject_code\":\"multiple_matches\""));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "narrow the patch target to a unique snippet"));
}

TEST_F(AgentLoopLimitsTest, ApplyPatchWritebackFailureStillStops) {
    const auto path = std::filesystem::path(test_workspace) / "readonly.txt";
    write_file("readonly.txt", "hello");
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace);

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        turn++;
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {{
                {"id", "patch_writeback"},
                {"function", {
                    {"name", "apply_patch"},
                    {"arguments", "{\"path\":\"readonly.txt\",\"old_text\":\"hello\",\"new_text\":\"bye\"}"}
                }}
            }}}
        };
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        std::filesystem::perm_options::replace);

    EXPECT_EQ(turn, 1);
}

TEST_F(AgentLoopLimitsTest, BuildFailureContinuesWithInspectionGuidance) {
    write_build_script(
        "printf 'compile failed\\n' 1>&2\n"
        "exit 7\n");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "build_fail"},
                    {"function", {
                        {"name", "build_project_safe"},
                        {"arguments", "{}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"status\":\"failed\""));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "inspect summary/stdout/stderr"));
}

TEST_F(AgentLoopLimitsTest, TestFailureContinuesWithFailedTestsGuidance) {
    write_build_script(
        "printf '50%% tests passed, 1 tests failed out of 2\\n'\n"
        "printf 'The following tests FAILED:\\n'\n"
        "printf '        1 - suite.alpha (Failed)\\n'\n"
        "exit 1\n");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "test_fail"},
                    {"function", {
                        {"name", "test_project_safe"},
                        {"arguments", "{}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"failed_tests\":[\"suite.alpha\"]"));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "suite.alpha"));
}

TEST_F(AgentLoopLimitsTest, TestFailureGuidanceEllipsizesAfterVisibleFailedTests) {
    write_build_script(
        "printf '20%% tests passed, 4 tests failed out of 5\\n'\n"
        "printf 'The following tests FAILED:\\n'\n"
        "printf '        1 - suite.alpha (Failed)\\n'\n"
        "printf '        2 - suite.beta (Failed)\\n'\n"
        "printf '        3 - suite.gamma (Failed)\\n'\n"
        "printf '        4 - suite.delta (Failed)\\n'\n"
        "exit 1\n");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "test_many_failures"},
                    {"function", {
                        {"name", "test_project_safe"},
                        {"arguments", "{}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    const std::string guidance = first_message_content_with_role(second_turn_messages, "system");
    EXPECT_EQ(turn, 2);
    EXPECT_NE(guidance.find("Reported failed_tests: suite.alpha, suite.beta, suite.gamma, ..."), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, TestFailureWithoutParsedFailedTestsOmitsMalformedFragment) {
    write_build_script(
        "printf 'tests failed but without ctest-style failed test names\\n'\n"
        "exit 1\n");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "test_no_failed_names"},
                    {"function", {
                        {"name", "test_project_safe"},
                        {"arguments", "{}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    const std::string guidance = first_message_content_with_role(second_turn_messages, "system");
    EXPECT_EQ(turn, 2);
    EXPECT_EQ(guidance.find("Reported failed_tests:"), std::string::npos);
    EXPECT_EQ(guidance.find(", ..."), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, BuildTimeoutContinuesWithRetryGuidance) {
    write_build_script(
        "sleep 2\n"
        "exit 0\n");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "build_timeout"},
                    {"function", {
                        {"name", "build_project_safe"},
                        {"arguments", "{\"timeout_ms\":50}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"timed_out\":true"));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "classification=retryable"));
}

TEST_F(AgentLoopLimitsTest, RepeatedEquivalentFailureStopsAfterRetryBudget) {
    write_file("repeat.txt", "hello");
    const std::string huge_patch_text(512, 'p');

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 5;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        turn++;
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {{
                {"id", "repeat_patch"},
                {"function", {
                    {"name", "apply_patch"},
                    {"arguments", nlohmann::json{
                        {"path", "repeat.txt"},
                        {"old_text", "missing"},
                        {"new_text", huge_patch_text}
                    }.dump()}
                }}
            }}}
        };
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
}

TEST_F(AgentLoopLimitsTest, BoundarySizedArgumentsStillRetryAsEquivalentFailures) {
    write_file("boundary.txt", "hello");
    const std::string boundary_patch_text(128, 'q');

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 5;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {{
                {"id", "boundary_patch"},
                {"function", {
                    {"name", "apply_patch"},
                    {"arguments", nlohmann::json{
                        {"path", "boundary.txt"},
                        {"old_text", "missing"},
                        {"new_text", boundary_patch_text}
                    }.dump()}
                }}
            }}}
        };
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
}

TEST_F(AgentLoopLimitsTest, BlockedToolContinuesWithGuidance) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "blocked_write"},
                    {"function", {
                        {"name", "write_file_safe"},
                        {"arguments", "{\"path\":\"blocked.txt\",\"content\":\"x\"}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"status\":\"blocked\""));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "classification=blocked"));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "Do not repeat the blocked call unchanged"));
}

TEST_F(AgentLoopLimitsTest, RepeatedBlockedFailureStopsAfterRetryBudget) {
    const std::string huge_content(1024, 'x');

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 5;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {{
                {"id", "blocked_repeat"},
                {"function", {
                    {"name", "write_file_safe"},
                    {"arguments", nlohmann::json{
                        {"path", "blocked.txt"},
                        {"content", huge_content}
                    }.dump()}
                }}
            }}}
        };
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
}

TEST_F(AgentLoopLimitsTest, UnsupportedStructuredFailureRemainsFatal) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {{
                {"id", "bash_fail"},
                {"function", {
                    {"name", "bash_execute_safe"},
                    {"arguments", "{\"command\":\"exit 7\",\"timeout_ms\":1000}"}
                }}
            }}}
        };
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 1);
}

TEST_F(AgentLoopLimitsTest, MalformedApplyPatchArgumentsRemainFatal) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {{
                {"id", "bad_args"},
                {"function", {
                    {"name", "apply_patch"},
                    {"arguments", "{not valid json"}
                }}
            }}}
        };
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 1);
}

TEST_F(AgentLoopLimitsTest, DifferentRecoverableFailuresDoNotShareRetryBudget) {
    write_file("fingerprint.txt", "dup dup");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 4;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "first_recoverable"},
                    {"function", {
                        {"name", "apply_patch"},
                        {"arguments", "{\"path\":\"fingerprint.txt\",\"old_text\":\"missing\",\"new_text\":\"x\"}"}
                    }}
                }}}
            };
        }
        if (turn == 2) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "second_recoverable"},
                    {"function", {
                        {"name", "apply_patch"},
                        {"arguments", "{\"path\":\"fingerprint.txt\",\"old_text\":\"dup\",\"new_text\":\"x\"}"}
                    }}
                }}}
            };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 3);
}

TEST_F(AgentLoopLimitsTest, SameRecoverableFailureWithChangedArgumentsDoesNotCountAsIdentical) {
    write_file("arg_change.txt", "alpha beta");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 4;

    int turn = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "no_match_one"},
                    {"function", {
                        {"name", "apply_patch"},
                        {"arguments", "{\"path\":\"arg_change.txt\",\"old_text\":\"missing1\",\"new_text\":\"x\"}"}
                    }}
                }}}
            };
        }
        if (turn == 2) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "no_match_two"},
                    {"function", {
                        {"name", "apply_patch"},
                        {"arguments", "{\"path\":\"arg_change.txt\",\"old_text\":\"missing2\",\"new_text\":\"x\"}"}
                    }}
                }}}
            };
        }
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 3);
}

TEST_F(AgentLoopLimitsTest, GuidanceMessageIsAdditiveNotSubstitutive) {
    write_file("additive.txt", "hello");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "additive_patch"},
                    {"function", {
                        {"name", "apply_patch"},
                        {"arguments", "{\"path\":\"additive.txt\",\"old_text\":\"missing\",\"new_text\":\"x\"}"}
                    }}
                }}}
            };
        }
        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_EQ(count_messages_with_role(second_turn_messages, "tool"), 1);
    EXPECT_GE(count_messages_with_role(second_turn_messages, "system"), 1);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"reject_code\":\"no_match\""));
    EXPECT_TRUE(message_content_contains(second_turn_messages, "system", "Tool recovery guidance for apply_patch"));
}

TEST_F(AgentLoopLimitsTest, GuidanceMessageStaysSmallAndActionable) {
    write_build_script(
        "printf '50%% tests passed, 1 tests failed out of 2\\n'\n"
        "printf 'The following tests FAILED:\\n'\n"
        "printf '        1 - suite.alpha (Failed)\\n'\n"
        "printf 'very long raw stdout that should remain in the tool message only\\n'\n"
        "exit 1\n");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.allow_execution_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "test_guidance"},
                    {"function", {
                        {"name", "test_project_safe"},
                        {"arguments", "{}"}
                    }}
                }}}
            };
        }
        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    const std::string guidance = first_message_content_with_role(second_turn_messages, "system");
    EXPECT_EQ(turn, 2);
    EXPECT_NE(guidance.find("Tool recovery guidance for test_project_safe"), std::string::npos);
    EXPECT_NE(guidance.find("Reported failed_tests: suite.alpha."), std::string::npos);
    EXPECT_EQ(guidance.find("very long raw stdout"), std::string::npos);
    EXPECT_LT(guidance.size(), 300u);
}

TEST_F(AgentLoopLimitsTest, RecoverableFailureStopsRemainingToolCallsInTurn) {
    write_file("batch.txt", "hello");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        turn++;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "recoverable_first"},
                        {"function", {
                            {"name", "apply_patch"},
                            {"arguments", "{\"path\":\"batch.txt\",\"old_text\":\"missing\",\"new_text\":\"patched\"}"}
                        }}
                    },
                    {
                        {"id", "should_not_run"},
                        {"function", {
                            {"name", "write_file_safe"},
                            {"arguments", "{\"path\":\"should_not_exist.txt\",\"content\":\"x\"}"}
                        }}
                    }
                }}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(test_workspace) / "should_not_exist.txt"));
    EXPECT_EQ(count_messages_with_role(second_turn_messages, "tool"), 2);
    EXPECT_TRUE(message_content_contains(second_turn_messages, "tool", "\"reject_code\":\"no_match\""));
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "should_not_run").find("\"status\":\"skipped\""), std::string::npos);
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "should_not_run").find("not executed due to prior tool failure"), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, RecoverableFailureOnFirstToolSynthesizesSkippedResponsesForRemainingCalls) {
    write_file("multi_skip.txt", "hello");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "recoverable_first"},
                        {"function", {
                            {"name", "apply_patch"},
                            {"arguments", "{\"path\":\"multi_skip.txt\",\"old_text\":\"missing\",\"new_text\":\"patched\"}"}
                        }}
                    },
                    {
                        {"id", "skipped_second"},
                        {"function", {
                            {"name", "write_file_safe"},
                            {"arguments", "{\"path\":\"should_not_exist_2.txt\",\"content\":\"x\"}"}
                        }}
                    },
                    {
                        {"id", "skipped_third"},
                        {"function", {
                            {"name", "write_file_safe"},
                            {"arguments", "{\"path\":\"should_not_exist_3.txt\",\"content\":\"y\"}"}
                        }}
                    }
                }}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_EQ(count_messages_with_role(second_turn_messages, "tool"), 3);
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "recoverable_first").find("\"reject_code\":\"no_match\""), std::string::npos);
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "skipped_second").find("\"status\":\"skipped\""), std::string::npos);
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "skipped_third").find("\"status\":\"skipped\""), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(test_workspace) / "should_not_exist_2.txt"));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(test_workspace) / "should_not_exist_3.txt"));
}

TEST_F(AgentLoopLimitsTest, RecoverableFailureOnLastToolDoesNotAddExtraSkippedResponse) {
    write_file("last_tool.txt", "hello");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.allow_mutating_tools = true;
    config.max_turns = 3;

    int turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        ++turn;
        if (turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {
                        {"id", "read_ok"},
                        {"function", {
                            {"name", "read_file_safe"},
                            {"arguments", "{\"path\":\"last_tool.txt\"}"}
                        }}
                    },
                    {
                        {"id", "recoverable_last"},
                        {"function", {
                            {"name", "apply_patch"},
                            {"arguments", "{\"path\":\"last_tool.txt\",\"old_text\":\"missing\",\"new_text\":\"patched\"}"}
                        }}
                    }
                }}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "", "", nlohmann::json::array(), mock_llm);

    EXPECT_EQ(turn, 2);
    EXPECT_EQ(count_messages_with_role(second_turn_messages, "tool"), 2);
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "read_ok").find("hello"), std::string::npos);
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "recoverable_last").find("\"reject_code\":\"no_match\""), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, CombinedTruncationAndContextLimit) {
    write_file("big_a.txt", std::string(400, 'A'));
    write_file("big_b.txt", std::string(400, 'B'));
    write_file("big_c.txt", std::string(400, 'C'));

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 2; // Need a second turn to test context clipping
    config.max_tool_output_bytes = 50; // Severely truncate individual tools
    config.max_context_bytes = 500;    // Tightly clamp total context
    
    int loop_call_count = 0;
    int compaction_call_count = 0;
    size_t msgs_size_turn_2 = 0;
    
    LLMStreamFunc mock_llm = [&](const AgentConfig& cfg, const nlohmann::json& msgs, const nlohmann::json&) -> nlohmann::json {
        const std::string first_system = first_message_content_with_role(msgs, "system");
        if (first_system.find("context compactor") != std::string::npos) {
            compaction_call_count++;
            return nlohmann::json{
                {"role", "assistant"},
                {"content", "Earlier reads covered big_a.txt, big_b.txt, and big_c.txt; outputs were truncated."}
            };
        }

        loop_call_count++;
        if (loop_call_count == 1) {
            // First turn: generate lots of tools with heavy output
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {{"id", "t1"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"big_a.txt\"}"}}}},
                    {{"id", "t2"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"big_b.txt\"}"}}}},
                    {{"id", "t3"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"big_c.txt\"}"}}}}
                }}
            };
        } else {
            // Second turn: analyze sliding window results
            msgs_size_turn_2 = msgs.dump().size();
            return nlohmann::json{{"role", "assistant"}, {"content", "Stop!"}};
        }
    };
    
    agent_run(config, "Sys", "Req", nlohmann::json::array(), mock_llm);
    
    // Expect agent went to turn 2
    EXPECT_EQ(loop_call_count, 2);
    EXPECT_EQ(compaction_call_count, 1);
    
    // Validate output shrinkage actually cascaded nicely below global max limitation
    EXPECT_LE(msgs_size_turn_2, config.max_context_bytes);
    EXPECT_GT(msgs_size_turn_2, 0);
}

TEST_F(AgentLoopLimitsTest, DelegateSubagentChildFiltersToolsAndReturnsStructuredResult) {
    write_file("delegate.txt", "delegated content");

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 4;
    config.max_tool_calls_per_turn = 4;
    config.max_total_tool_calls = 8;

    int parent_turn = 0;
    int child_turn = 0;
    bool child_saw_delegate = false;
    nlohmann::json second_turn_messages = nlohmann::json::array();

    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json& tools) -> nlohmann::json {
        const std::string first_system = first_message_content_with_role(messages, "system");
        if (first_system.find("temporary delegated subagent") != std::string::npos) {
            child_turn++;
            for (const auto& tool : tools) {
                if (tool["function"]["name"] == "delegate_subagent") {
                    child_saw_delegate = true;
                }
            }

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
                 "{\"ok\":true,\"result_summary\":\"Read delegate.txt for the parent.\","
                 "\"files_touched\":[\"delegate.txt\"],"
                 "\"key_facts\":[\"delegate.txt contains delegated content\"],"
                 "\"open_questions\":[],"
                 "\"commands_ran\":[],"
                 "\"verification_passed\":true,"
                 "\"error\":\"\"}"}
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
                         "{\"role\":\"researcher\",\"task\":\"Inspect delegate.txt\","
                         "\"context_files\":[\"delegate.txt\"],\"expected_output\":\"Return the file finding\","
                         "\"max_turns\":2}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "parent system", "delegate the side task", get_agent_tools_schema(), mock_llm);

    ASSERT_EQ(parent_turn, 2);
    ASSERT_EQ(child_turn, 2);
    EXPECT_FALSE(child_saw_delegate);

    const auto result = nlohmann::json::parse(tool_message_content_for_id(second_turn_messages, "delegate_call"));
    EXPECT_TRUE(result["ok"].get<bool>()) << result.dump();
    EXPECT_EQ(result["role"], "researcher");
    EXPECT_EQ(result["task"], "Inspect delegate.txt");
    EXPECT_EQ(result["result_summary"], "Read delegate.txt for the parent.");
    EXPECT_EQ(result["files_touched"], nlohmann::json::array({"delegate.txt"}));
    EXPECT_EQ(result["key_facts"], nlohmann::json::array({"delegate.txt contains delegated content"}));
    EXPECT_EQ(result["commands_ran"], nlohmann::json::array());
    EXPECT_TRUE(result["verification_passed"].get<bool>());
}

TEST_F(AgentLoopLimitsTest, ChildCannotRecursivelyDelegate) {
    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 4;
    config.max_tool_calls_per_turn = 4;
    config.max_total_tool_calls = 8;

    int parent_turn = 0;
    int child_turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();

    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        const std::string first_system = first_message_content_with_role(messages, "system");
        if (first_system.find("temporary delegated subagent") != std::string::npos) {
            child_turn++;
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {{
                    {"id", "child_delegate_again"},
                    {"function", {
                        {"name", "delegate_subagent"},
                        {"arguments", "{\"role\":\"nested\",\"task\":\"Should fail\"}"}
                    }}
                }}}
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
                        {"arguments", "{\"role\":\"researcher\",\"task\":\"Attempt recursion\"}"}
                    }}
                }}}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config, "parent system", "delegate the side task", get_agent_tools_schema(), mock_llm);

    ASSERT_EQ(parent_turn, 2);
    ASSERT_EQ(child_turn, 1);

    const auto result = nlohmann::json::parse(tool_message_content_for_id(second_turn_messages, "delegate_call"));
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_NE(result["error"].get<std::string>().find("ended without a final summary"), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, CompactionPreservesSystemPromptAndRecentMessages) {
    write_file("recent1.txt", "one" + std::string(100, 'x'));
    write_file("recent2.txt", "two" + std::string(100, 'y'));
    write_file("recent3.txt", "three" + std::string(100, 'z'));
    write_file("recent4.txt", "four" + std::string(100, 'w'));
    write_file("recent5.txt", "five" + std::string(100, 'v'));

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 2;
    config.max_tool_calls_per_turn = 8;
    config.max_total_tool_calls = 8;
    config.max_context_bytes = 1000;

    int loop_turn = 0;
    int compaction_turn = 0;
    nlohmann::json second_turn_messages = nlohmann::json::array();

    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        const std::string first_system = first_message_content_with_role(messages, "system");
        if (first_system.find("context compactor") != std::string::npos) {
            compaction_turn++;
            return nlohmann::json{
                {"role", "assistant"},
                {"content", "Summary preserved src/main.cpp, bash -lc ./build.sh test, and compile failed."}
            };
        }

        loop_turn++;
        if (loop_turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {{"id", "recent1"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"recent1.txt\"}"}}}},
                    {{"id", "recent2"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"recent2.txt\"}"}}}},
                    {{"id", "recent3"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"recent3.txt\"}"}}}},
                    {{"id", "recent4"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"recent4.txt\"}"}}}},
                    {{"id", "recent5"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"recent5.txt\"}"}}}}
                }}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config,
              "PRIMARY SYSTEM PROMPT",
              "Earlier decision: inspect src/main.cpp after bash -lc ./build.sh test failed with compile failed.",
              get_agent_tools_schema(false),
              mock_llm);

    EXPECT_EQ(loop_turn, 2);
    EXPECT_EQ(compaction_turn, 1);
    EXPECT_EQ(nth_message_content_with_role(second_turn_messages, "system", 0), "PRIMARY SYSTEM PROMPT");
    EXPECT_NE(nth_message_content_with_role(second_turn_messages, "system", 1).find("Compacted earlier context summary"), std::string::npos);
    EXPECT_NE(tool_message_content_for_id(second_turn_messages, "recent5").find("five"), std::string::npos);
}

TEST_F(AgentLoopLimitsTest, CompactionSummaryKeepsImportantFacts) {
    write_file("facts1.txt", "fact one" + std::string(100, 'a'));
    write_file("facts2.txt", "fact two" + std::string(100, 'b'));
    write_file("facts3.txt", "fact three" + std::string(100, 'c'));
    write_file("facts4.txt", "fact four" + std::string(100, 'd'));
    write_file("facts5.txt", "fact five" + std::string(100, 'e'));

    AgentConfig config;
    config.workspace_abs = test_workspace;
    config.max_turns = 2;
    config.max_tool_calls_per_turn = 8;
    config.max_total_tool_calls = 8;
    config.max_context_bytes = 1000;

    int loop_turn = 0;
    std::string compaction_request;
    nlohmann::json second_turn_messages = nlohmann::json::array();

    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json& messages, const nlohmann::json&) -> nlohmann::json {
        const std::string first_system = first_message_content_with_role(messages, "system");
        if (first_system.find("context compactor") != std::string::npos) {
            compaction_request = messages[1].value("content", "");
            return nlohmann::json{
                {"role", "assistant"},
                {"content", "Use src/main.cpp, rerun bash -lc ./build.sh test, and note compile failed."}
            };
        }

        loop_turn++;
        if (loop_turn == 1) {
            return nlohmann::json{
                {"role", "assistant"},
                {"tool_calls", {
                    {{"id", "facts1"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"facts1.txt\"}"}}}},
                    {{"id", "facts2"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"facts2.txt\"}"}}}},
                    {{"id", "facts3"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"facts3.txt\"}"}}}},
                    {{"id", "facts4"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"facts4.txt\"}"}}}},
                    {{"id", "facts5"}, {"function", {{"name", "read_file_safe"}, {"arguments", "{\"path\":\"facts5.txt\"}"}}}}
                }}
            };
        }

        second_turn_messages = messages;
        return nlohmann::json{{"role", "assistant"}, {"content", "done"}};
    };

    agent_run(config,
              "PRIMARY SYSTEM PROMPT",
              "Remember src/main.cpp; command bash -lc ./build.sh test; latest error compile failed.",
              get_agent_tools_schema(false),
              mock_llm);

    ASSERT_EQ(loop_turn, 2);
    EXPECT_NE(compaction_request.find("src/main.cpp"), std::string::npos);
    EXPECT_NE(compaction_request.find("bash -lc ./build.sh test"), std::string::npos);
    EXPECT_NE(compaction_request.find("compile failed"), std::string::npos);

    const std::string summary = nth_message_content_with_role(second_turn_messages, "system", 1);
    EXPECT_NE(summary.find("src/main.cpp"), std::string::npos);
    EXPECT_NE(summary.find("bash -lc ./build.sh test"), std::string::npos);
    EXPECT_NE(summary.find("compile failed"), std::string::npos);
}
