#include <gtest/gtest.h>
#include "agent_loop.hpp"
#include "agent_utils.hpp"
#include "config.hpp"
#include "logger.hpp"

class AgentLoopLimitsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Suppress stdout if needed
    }
};

TEST_F(AgentLoopLimitsTest, MaxTurnsHalt) {
    AgentConfig config;
    config.max_turns = 3;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 50;
    config.max_context_bytes = 100000;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // Always returns a tool call to loop infinitely
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {
                    {"id", "call_" + std::to_string(call_count)},
                    {"type", "function"},
                    {"function", {
                        {"name", "dummy_tool"},
                        {"arguments", "{}"}
                    }}
                }
            }}
        };
    };
    
    agent_run(config, "System", "Prompt", nlohmann::json::array(), mock_llm);
    
    // Should exactly hit 3 turns and exit
    EXPECT_EQ(call_count, 3);
}

TEST_F(AgentLoopLimitsTest, MaxToolCallsPerTurn) {
    AgentConfig config;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 2; // VERY LOW
    config.max_total_tool_calls = 50;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // Return 3 tools, which exceeds 2
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "1"}, {"function", {{"name", "t1"}}}},
                {{"id", "2"}, {"function", {{"name", "t2"}}}},
                {{"id", "3"}, {"function", {{"name", "t3"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Fails fast on turn 1
    EXPECT_EQ(call_count, 1);
}

TEST_F(AgentLoopLimitsTest, MaxTotalToolCalls) {
    AgentConfig config;
    config.max_turns = 10;
    config.max_tool_calls_per_turn = 5;
    config.max_total_tool_calls = 7;
    
    int call_count = 0;
    LLMStreamFunc mock_llm = [&](const AgentConfig&, const nlohmann::json&, const nlohmann::json&) -> nlohmann::json {
        call_count++;
        // 4 tools per turn
        return nlohmann::json{
            {"role", "assistant"},
            {"tool_calls", {
                {{"id", "1"}, {"function", {{"name", "dummy"}}}},
                {{"id", "2"}, {"function", {{"name", "dummy"}}}},
                {{"id", "3"}, {"function", {{"name", "dummy"}}}},
                {{"id", "4"}, {"function", {{"name", "dummy"}}}}
            }}
        };
    };
    
    agent_run(config, "", "", nlohmann::json::array(), mock_llm);
    
    // Turn 1: 4 tools -> total = 4 (OK)
    // Turn 2: 4 tools -> total = 8 (> 7). Loop exits AT turn 2.
    EXPECT_EQ(call_count, 2);
}

TEST_F(AgentLoopLimitsTest, ToolOutputTruncation) {
    std::string huge(20000, 'A');
    std::string out = truncate_tool_output(huge, 100);
    EXPECT_TRUE(out.size() < 20000);
    EXPECT_TRUE(out.find("<TRUNCATED: original=20000 bytes, kept=100 bytes>") != std::string::npos);
}

TEST_F(AgentLoopLimitsTest, ContextSlidingWindow) {
    nlohmann::json messages = {
        {{"role", "system"}, {"content", "sys"}},
        {{"role", "user"}, {"content", "req"}},
        {{"role", "tool"}, {"content", std::string(1000, 'B')}}, // 1000 bytes padding
        {{"role", "tool"}, {"content", std::string(1000, 'C')}}
    };
    
    // Force constraint to be very small, e.g. 500 bytes
    enforce_context_limits(messages, 500);
    
    // The two tool outputs should be replaced with drops
    EXPECT_EQ(messages[2]["content"], "<DROPPED old tool outputs to fit context>");
}
