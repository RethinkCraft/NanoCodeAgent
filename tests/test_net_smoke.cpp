#include <gtest/gtest.h>
#include "llm.hpp"
#include "config.hpp"
#include <cstdlib>
#include <nlohmann/json.hpp>

TEST(NetSmokeTest, LiveStreamingCompletion) {
    if (const char* env_run = std::getenv("NCA_RUN_NET_TESTS")) {
        if (std::string(env_run) != "1") {
            GTEST_SKIP() << "NCA_RUN_NET_TESTS!=1, skipping real network test.";
        }
    } else {
        GTEST_SKIP() << "NCA_RUN_NET_TESTS not set, skipping real network test.";
    }
    
    const char* api_key = std::getenv("NCA_API_KEY");
    if (!api_key || std::string(api_key).empty()) {
        GTEST_SKIP() << "NCA_API_KEY is not set, skipping real network test.";
    }

    AgentConfig config;
    config.api_key = api_key;
    if (const char* url = std::getenv("NCA_BASE_URL")) config.base_url = url;
    else config.base_url = "https://api.openai.com/v1";
    
    if (const char* model = std::getenv("NCA_MODEL")) config.model = model;
    else config.model = "gpt-4o-mini";

    nlohmann::json msgs = nlohmann::json::array();
    msgs.push_back({
        {"role", "user"},
        {"content", "Say exactly 'hello' and nothing else."}
    });

    nlohmann::json no_tools = nlohmann::json::array();

    bool got_content = false;
    auto on_delta = [&](const std::string& txt) -> bool {
        if (!txt.empty()) {
            got_content = true;
        }
        return true;
    };

    EXPECT_NO_THROW({
        nlohmann::json response = llm_chat_completion_stream(config, msgs, no_tools, on_delta);
        EXPECT_TRUE(response.contains("role"));
        EXPECT_EQ(response["role"], "assistant");
        if (response.contains("content")) {
            std::string content = response["content"].get<std::string>();
            EXPECT_NE(content, "");
        } else {
            // It could be missing if it only gave a tool call, but we gave no tools here
            // So we expect content.
            EXPECT_TRUE(got_content);
        }
    }) << "The stream should finish without an exception.";
}
