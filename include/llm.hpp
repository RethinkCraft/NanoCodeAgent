#pragma once
#include "config.hpp"
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

class SseParser;
class ToolCallAssembler;

// Parse API JSON response (legacy non-streaming)
bool llm_parse_response(const std::string& json_resp, std::string* out_text, std::string* err);

// Exposed for Mock Tests: Parse chunk and call the content callback.
bool llm_stream_process_chunk(const std::string& chunk, SseParser& parser, const std::function<bool(const std::string&)>& on_content_delta, ToolCallAssembler* tool_asm, std::string* err);

// Day 10 SSE Streaming API for Agent
// Returns a materialised JSON response message representing the assistant's turn (with content/tool_calls)
// Throws std::runtime_error on network errors or parsing failure.
nlohmann::json llm_chat_completion_stream(
    const AgentConfig& cfg, 
    const nlohmann::json& messages,
    const nlohmann::json& tools,
    std::function<bool(const std::string&)> on_content_delta
);
