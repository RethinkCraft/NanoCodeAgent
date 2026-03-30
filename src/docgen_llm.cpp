#include "docgen_llm.hpp"
#include "llm.hpp"
#include "http.hpp"
#include "logger.hpp"
#include "sse_parser.hpp"
#include <sstream>
#include <regex>

namespace docgen {

LlmClient::LlmClient(const AgentConfig& cfg, const SubagentContext& ctx)
    : config_(cfg), ctx_(ctx) {}

std::expected<nlohmann::json, std::string> LlmClient::call(
    const std::string& system_prompt,
    const nlohmann::json& user_context,
    StreamCallback on_delta
) {
    if (!config_.api_key.has_value() || config_.api_key->empty()) {
        return std::unexpected("API key is not configured");
    }

    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt}});
    messages.push_back({{"role", "user"}, {"content", user_context.dump()}});

    nlohmann::json req = {
        {"model", config_.model},
        {"messages", messages},
        {"stream", true}
    };

    std::string url = config_.base_url;
    if (!url.empty() && url.back() == '/') url.pop_back();
    if (url.find("/chat/completions") == std::string::npos) {
        url += "/chat/completions";
    }

    std::string payload = req.dump();
    std::vector<std::string> headers = {
        "Content-Type: application/json",
        "Authorization: Bearer " + *config_.api_key
    };

    SseParser parser;
    std::string full_content;
    std::string err_msg;
    bool process_ok = true;

    HttpOptions opts;
    opts.timeout_ms = ctx_.timeout_ms;

    bool req_ok = http_post_json_stream(
        url, headers, payload, opts,
        [&](const std::string& chunk) {
            std::string local_err;
            auto events = parser.feed(chunk);
            for (const auto& event : events) {
                if (event == "[DONE]") return true;
                
                try {
                    auto parsed = nlohmann::json::parse(event);
                    if (parsed.contains("error")) {
                        err_msg = "API error: " + parsed["error"].dump();
                        return false;
                    }
                    if (parsed.contains("choices") && !parsed["choices"].empty()) {
                        auto delta = parsed["choices"][0].value("delta", nlohmann::json{});
                        if (delta.contains("content") && delta["content"].is_string()) {
                            std::string txt = delta["content"].get<std::string>();
                            full_content += txt;
                            if (on_delta && !on_delta(txt)) {
                                return false;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    err_msg = "Parse error: " + std::string(e.what());
                    return false;
                }
            }
            return true;
        },
        &err_msg
    );

    if (!req_ok) {
        return std::unexpected("Network error: " + err_msg);
    }
    if (!process_ok) {
        return std::unexpected(err_msg);
    }

    return nlohmann::json{{"content", full_content}};
}

std::expected<nlohmann::json, std::string> LlmClient::call_json(
    const std::string& system_prompt,
    const nlohmann::json& user_context,
    StreamCallback on_delta
) {
    auto resp = call(system_prompt, user_context, on_delta);
    if (!resp) {
        return std::unexpected(resp.error());
    }

    std::string content = (*resp)["content"].get<std::string>();
    std::string json_str = extract_json_from_response(content);

    try {
        return nlohmann::json::parse(json_str);
    } catch (const std::exception& e) {
        return std::unexpected("JSON parse error: " + std::string(e.what()) + " in: " + json_str.substr(0, 200));
    }
}

std::string LlmClient::extract_json_from_response(const std::string& content) {
    // 1. Try to extract JSON from Markdown code block first
    std::regex json_block(R"(```(?:json)?\s*(\{[\s\S]*?\})\s*```)");
    std::smatch match;
    if (std::regex_search(content, match, json_block)) {
        return match[1].str();
    }
    
    // 2. Fallback: find the outermost braces from first '{' to last '}'
    const size_t first_brace = content.find('{');
    if (first_brace == std::string::npos) {
        return content; // No opening brace found
    }
    
    const size_t last_brace = content.rfind('}');
    if (last_brace == std::string::npos || last_brace <= first_brace) {
        return content; // No matching closing brace
    }
    
    // Return the substring including both braces
    return content.substr(first_brace, last_brace - first_brace + 1);
}

} // namespace docgen
