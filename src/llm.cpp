#include "llm.hpp"
#include "http.hpp"
#include "logger.hpp"
#include "sse_parser.hpp"
#include "tool_call.hpp"
#include "tool_call_assembler.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>

using json = nlohmann::json;

bool llm_parse_response(const std::string& json_resp, std::string* out_text, std::string* err) {
    try {
        auto parsed = json::parse(json_resp);
        if (parsed.contains("error")) {
            if (err) *err = "API Error: " + parsed["error"].dump();
            return false;
        }
        if (parsed.contains("choices") && parsed["choices"].is_array() && parsed["choices"].size() > 0) {
            auto first_choice = parsed["choices"][0];
            if (first_choice.contains("message") && first_choice["message"].contains("content")) {
                if (first_choice["message"]["content"].is_string()) {
                    if (out_text) *out_text = first_choice["message"]["content"].get<std::string>();
                    return true;
                }
            }
        }
        if (err) *err = "Response missing choices[0].message.content";
        return false;
    } catch (const std::exception& e) {
        if (err) *err = "JSON parse error: " + std::string(e.what());
        return false;
    }
}

bool llm_stream_process_chunk(const std::string& chunk, SseParser& parser, const std::function<bool(const std::string&)>& on_content_delta, ToolCallAssembler* tool_asm, std::string* err) {
    auto events = parser.feed(chunk);
    
    for (const auto& event : events) {
        if (event == "[DONE]") {
            return true; 
        }

        try {
            auto parsed = json::parse(event);
            if (parsed.contains("error")) {
                if (err) {
                    *err = "API Error: ";
                    if (parsed["error"].contains("message") && parsed["error"]["message"].is_string()) {
                        *err += parsed["error"]["message"].get<std::string>();
                    } else {
                        *err += parsed["error"].dump();
                    }
                }
                return false;
            }

            if (parsed.contains("choices") && parsed["choices"].is_array() && parsed["choices"].size() > 0) {
                auto first_choice = parsed["choices"][0];
                if (first_choice.contains("delta")) {
                    auto delta = first_choice["delta"];
                    
                    if (delta.contains("content") && delta["content"].is_string()) {
                        std::string delta_text = delta["content"].get<std::string>();
                        if (!delta_text.empty() && on_content_delta) {
                            if (!on_content_delta(delta_text)) {
                                if (err) *err = "User aborted stream";
                                return false;
                            }
                        }
                    }
                    
                    if (tool_asm && delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
                        for (const auto& tcd : delta["tool_calls"]) {
                            if (!tool_asm->ingest_delta(tcd, err)) return false;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            if (err) *err = "Stream Parse Error: " + std::string(e.what()) + " Event: " + event;
            return false;
        }
    }
    return true;
}

json llm_chat_completion_stream(
    const AgentConfig& cfg, 
    const json& messages,
    const json& tools,
    std::function<bool(const std::string&)> on_content_delta
) {
    if (!cfg.api_key.has_value() || cfg.api_key.value().empty()) {
        throw std::runtime_error("API key is not configured.");
    }

    json req_json = {
        {"model", cfg.model},
        {"messages", messages},
        {"stream", true}
    };
    if (!tools.empty()) {
        req_json["tools"] = tools;
    }

    std::string payload = req_json.dump();
    std::vector<std::string> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " + cfg.api_key.value()
    };

    std::string url = cfg.base_url;
    if (!url.empty() && url.back() == '/') url.pop_back();
    
    // Sometimes people configure --base-url https://api.openai.com/v1
    // We append /chat/completions. If base_url already contains it, don't append.
    if (url.find("/chat/completions") == std::string::npos) {
        url += "/chat/completions";
    }

    SseParser sse_parser;
    ToolCallAssembler asm_tools;
    std::string err_msg;
    bool process_ok = true;
    
    std::string full_content;

    if (cfg.debug_mode) {
        LOG_DEBUG("LLM Req URL: {}", url);
    }

    HttpOptions http_opts;
    bool req_ok = http_post_json_stream(
        url,
        headers,
        payload,
        http_opts,
        [&](const std::string& chunk_data) {
            // Intercept content_delta to also accumulate the full_content locally
            auto local_cb = [&](const std::string& txt) -> bool {
                full_content += txt;
                if (on_content_delta) return on_content_delta(txt);
                return true;
            };
            bool ok = llm_stream_process_chunk(chunk_data, sse_parser, local_cb, &asm_tools, &err_msg);
            if (!ok) process_ok = false;
            return ok; // false aborts the curl download
        },
        &err_msg
    );

    if (!req_ok) {
        throw std::runtime_error("Network request failed: " + err_msg);
    }
    if (!process_ok) {
        throw std::runtime_error("Stream processing failed: " + err_msg);
    }

    // Attempt to assemble tools. If their json args are busted, throw runtime_error
    std::vector<ToolCall> tools_res;
    if (!asm_tools.finalize(&tools_res, &err_msg)) {
        throw std::runtime_error("Tool finalization failed: " + err_msg);
    }

    json res = {
        {"role", "assistant"}
    };
    if (!full_content.empty()) {
        res["content"] = full_content;
    }

    if (!tools_res.empty()) {
        json tc_arr = json::array();
        for (const auto& tc : tools_res) {
            tc_arr.push_back({
                {"id", tc.id},
                {"type", "function"},
                {"function", {
                    {"name", tc.name},
                    {"arguments", tc.arguments.dump()}
                }}
            });
        }
        res["tool_calls"] = tc_arr;
    }

    return res;
}
