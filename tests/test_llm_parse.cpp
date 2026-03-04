#include <gtest/gtest.h>
#include "llm.hpp"
#include <fstream>
#include <sstream>

std::string load_fixture(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

TEST(LLMTest, ParseOkResponse) {
    std::string resp = load_fixture("../../tests/fixtures/resp_ok.json");
    ASSERT_FALSE(resp.empty()) << "Fixture not found";
    
    std::string out_text;
    std::string err;
    EXPECT_TRUE(llm_parse_response(resp, &out_text, &err));
    EXPECT_EQ(out_text, "Hello there, how may I assist you today?");
}

TEST(LLMTest, ParseErrorResponse) {
    std::string resp = load_fixture("../../tests/fixtures/resp_error.json");
    ASSERT_FALSE(resp.empty()) << "Fixture not found";
    
    std::string out_text;
    std::string err;
    EXPECT_FALSE(llm_parse_response(resp, &out_text, &err));
    EXPECT_NE(err.find("Incorrect API key"), std::string::npos);
}

