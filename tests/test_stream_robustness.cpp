#include <gtest/gtest.h>
#include "llm.hpp"
#include "sse_parser.hpp"
#include "tool_call.hpp"
#include "tool_call_assembler.hpp"
#include <nlohmann/json.hpp>
#include <vector>

using json = nlohmann::json;

class StreamRobustnessTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(StreamRobustnessTest, FragmentedChunks) {
    SseParser parser;
    ToolCallAssembler asm_tools;
    std::string err;
    std::string text_accum;

    std::function<bool(const std::string&)> cb = [&](const std::string& txt) -> bool {
        text_accum += txt;
        return true;
    };

    // Simulate SSE streamed byte-by-byte
    std::string payload = 
        "data: {\"choices\":[{\"delta\":{\"content\":\"He\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"llo\"}}]}\n\n"
        "data: [DONE]\n\n";

    // Feed it character by character (extreme fragmentation)
    for (char c : payload) {
        bool ok = llm_stream_process_chunk(std::string(1, c), parser, cb, &asm_tools, &err);
        EXPECT_TRUE(ok);
    }

    EXPECT_EQ(text_accum, "Hello");
}

TEST_F(StreamRobustnessTest, InterleavedToolCallDeltas) {
    SseParser parser;
    ToolCallAssembler asm_tools;
    std::string err;

    std::function<bool(const std::string&)> cb = [&](const std::string&) -> bool { return true; };

    std::vector<std::string> chunks = {
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\"read_file_safe\"}}]}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{\\\"path\\\"\"}}]}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\":\\\"test.txt\\\"}\"}}]}}]}\n\n",
        "data: [DONE]\n\n"
    };

    for (const auto& ch : chunks) {
        bool ok = llm_stream_process_chunk(ch, parser, cb, &asm_tools, &err);
        EXPECT_TRUE(ok);
    }

    std::vector<ToolCall> tools;
    ASSERT_TRUE(asm_tools.finalize(&tools, &err));
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools[0].id, "call_1");
    EXPECT_EQ(tools[0].name, "read_file_safe");
    EXPECT_EQ(tools[0].arguments.dump(), "{\"path\":\"test.txt\"}");
}

TEST_F(StreamRobustnessTest, BrokenJSONFailFast) {
    SseParser parser;
    ToolCallAssembler asm_tools;
    std::string err;

    std::function<bool(const std::string&)> cb = [&](const std::string&) -> bool { return true; };

    std::string payload = "data: {\"choices\":[{\"delta\":{ \n\n"; // Missing close braces
    bool ok = llm_stream_process_chunk(payload, parser, cb, &asm_tools, &err);
    
    // It should detect bad json and return false
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("Parse Error"), std::string::npos);
}

TEST_F(StreamRobustnessTest, IncompleteArgumentsToleratedDuringStreamButCatchedAtAssemble) {
    SseParser parser;
    ToolCallAssembler asm_tools;
    std::string err;

    std::function<bool(const std::string&)> cb = [&](const std::string&) -> bool { return true; };

    std::vector<std::string> chunks = {
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_2\",\"function\":{\"name\":\"read_file_safe\"}}]}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{\\\"path\\\"\"}}]}}]}\n\n"
        // STREAM SUDDENLY ENDS (no [DONE], no closing brace)
    };

    for (const auto& ch : chunks) {
        bool ok = llm_stream_process_chunk(ch, parser, cb, &asm_tools, &err);
        EXPECT_TRUE(ok);
    }

    // Now try to assemble
    std::vector<ToolCall> tools;
    bool ok = asm_tools.finalize(&tools, &err);
    EXPECT_FALSE(ok);
    EXPECT_NE(err.find("JSON parse failed"), std::string::npos);
}
