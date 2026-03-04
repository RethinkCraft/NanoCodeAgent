#include <gtest/gtest.h>
#include "agent_tools.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST(SchemaAndArgsToleranceTest, GetSchemaMatchesCurrentTools) {
    json schema = get_agent_tools_schema();
    EXPECT_TRUE(schema.is_array());
    EXPECT_EQ(schema.size(), 3);

    bool has_read = false, has_write = false, has_bash = false;
    for (const auto& tool : schema) {
        std::string name = tool["function"]["name"];
        if (name == "read_file_safe") has_read = true;
        if (name == "write_file_safe") has_write = true;
        if (name == "bash_execute_safe") has_bash = true;
    }
    EXPECT_TRUE(has_read);
    EXPECT_TRUE(has_write);
    EXPECT_TRUE(has_bash);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutToleratesStrings) {
    AgentConfig config;
    config.workspace_abs = "."; // Allow mock safe dir
    
    ToolCall tc;
    tc.name = "bash_execute_safe";
    // We explicitly pass timeout_ms as string instead of int
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "100"} 
    };
    
    std::string res = execute_tool(tc, config);
    // It should not throw and should safely execute
    EXPECT_NE(res.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(res.find("hello"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, MissingArgumentsReturnsErrorGracefully) {
    AgentConfig config;
    config.workspace_abs = "."; 
    
    ToolCall tc;
    tc.name = "read_file_safe";
    // Empty args!
    tc.arguments = json::object();
    
    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("Missing 'path' argument"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, TypeErrorsCaughtSafelyAndDontCrash) {
    AgentConfig config;
    config.workspace_abs = "."; 

    ToolCall tc;
    tc.name = "read_file_safe";
    // Path should be a string, we pass an object
    tc.arguments = {
        {"path", {{"unexpected", "object"}}} 
    };
    
    std::string res = execute_tool(tc, config);
    // Exception handled inside
    EXPECT_NE(res.find("failed"), std::string::npos);
}
