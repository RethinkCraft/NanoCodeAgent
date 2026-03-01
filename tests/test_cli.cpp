#include <gtest/gtest.h>
#include "cli.hpp"
#include "config.hpp"
#include <getopt.h>

class CliTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset getopt_long internal state for each test
        optind = 1;
    }
};

TEST_F(CliTest, ParseHelp) {
    AgentConfig config = config_init();
    const char* argv[] = {"agent", "--help"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    // Should return false to stop execution
    EXPECT_FALSE(cli_parse(argc, const_cast<char**>(argv), config));
}

TEST_F(CliTest, ParseExecute) {
    AgentConfig config = config_init();
    const char* argv[] = {"agent", "-e", "test prompt"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    EXPECT_TRUE(cli_parse(argc, const_cast<char**>(argv), config));
    EXPECT_EQ(config.prompt, "test prompt");
}

TEST_F(CliTest, ParseMissingExecute) {
    AgentConfig config = config_init();
    const char* argv[] = {"agent", "--model", "gpt-4"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    // Should return false because -e is missing
    EXPECT_FALSE(cli_parse(argc, const_cast<char**>(argv), config));
}

TEST_F(CliTest, ParseAllOptions) {
    AgentConfig config = config_init();
    const char* argv[] = {
        "agent", 
        "-e", "complex task",
        "-w", "/tmp/workspace",
        "--model", "claude-3",
        "--api-key", "sk-12345",
        "--base-url", "https://api.anthropic.com",
        "--debug"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    EXPECT_TRUE(cli_parse(argc, const_cast<char**>(argv), config));
    EXPECT_EQ(config.prompt, "complex task");
    EXPECT_EQ(config.workspace, "/tmp/workspace");
    EXPECT_EQ(config.model, "claude-3");
    EXPECT_EQ(config.api_key.value_or(""), "sk-12345");
    EXPECT_EQ(config.base_url, "https://api.anthropic.com");
    EXPECT_TRUE(config.debug_mode);
}
