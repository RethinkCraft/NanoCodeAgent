#pragma once

#include "config.hpp"
#include "docgen_types.hpp"
#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include <expected>

namespace docgen {

class LlmClient {
public:
    using StreamCallback = std::function<bool(const std::string& delta)>;

    LlmClient(const AgentConfig& cfg, const SubagentContext& ctx);
    
    std::expected<nlohmann::json, std::string> call(
        const std::string& system_prompt,
        const nlohmann::json& user_context,
        StreamCallback on_delta = nullptr
    );

    std::expected<nlohmann::json, std::string> call_json(
        const std::string& system_prompt,
        const nlohmann::json& user_context,
        StreamCallback on_delta = nullptr
    );

private:
    AgentConfig config_;
    SubagentContext ctx_;
    
    std::string extract_json_from_response(const std::string& content);
};

inline constexpr std::string_view kChangeAnalystPrompt = R"(
You are a precise code change analyst. Output ONLY valid JSON.

INPUT: git diff text

OUTPUT FORMAT (strict JSON):
{
  "affected_files": [
    {"path": "relative/path", "change_type": "add|modify|delete|rename", "old_path": "optional for rename", "public_symbols": ["optional list"]}
  ],
  "intent_summary": "one sentence describing the change purpose"
}

RULES:
1. Only include files with actual changes
2. change_type must be one of: add, modify, delete, rename
3. For renames, include old_path
4. public_symbols: only exportable/public symbols affected
5. NO prose, NO markdown code fences, ONLY the JSON object
)";

inline constexpr std::string_view kContextRouterPrompt = R"(
You are a document section locator. Output ONLY valid JSON.

INPUT:
- doc_outline: JSON with file_path and headings array
- intent_summary: what change is needed

OUTPUT FORMAT (strict JSON):
{
  "locations": [
    {"target_file": "path/to/doc.md", "start_line": N, "end_line": M, "section_heading": "optional heading"}
  ]
}

RULES:
1. Return only sections that need modification
2. start_line and end_line must be valid line numbers (1-based)
3. If no sections need changes, return {"locations": []}
4. NO prose, NO markdown code fences, ONLY the JSON object
)";

inline constexpr std::string_view kPatchWriterPrompt = 
"You are a precise document patch generator. Output ONLY valid JSON.\n\n"
"INPUT:\n"
"- original_text: the exact text block to modify (line-numbered)\n"
"- intent_summary: what change is needed\n\n"
"OUTPUT FORMAT (strict JSON):\n"
"{\n"
"  \"patches\": [\n"
"    {\n"
"      \"action\": \"replace|insert_before|insert_after|delete\",\n"
"      \"old_text\": \"exact text to find (for replace/delete)\",\n"
"      \"new_text\": \"replacement content (empty for delete)\"\n"
"    }\n"
"  ],\n"
"  \"rationale\": \"one-line explanation\"\n"
"}\n\n"
"RULES:\n"
"1. old_text MUST match EXACTLY (whitespace-sensitive) for replace/delete\n"
"2. Each patch operates on original_text independently (not sequential)\n"
"3. Use multiple patches only when truly independent\n"
"4. NO prose, NO markdown code fences, ONLY the JSON object\n"
"5. If no change needed, return {\"patches\": [], \"rationale\": \"no change required\"}";

inline constexpr std::string_view kMicroReviewerPrompt = 
"You are a minimal document patch reviewer. Output ONLY valid JSON.\n\n"
"INPUT:\n"
"- before: original text\n"
"- after: modified text\n"
"- intent_summary: intended change\n\n"
"OUTPUT FORMAT (strict JSON):\n"
"{\n"
"  \"verdict\": \"approve|reject\",\n"
"  \"reason\": \"one sentence\",\n"
"  \"issues\": [\"optional list of problems if rejected\"]\n"
"}\n\n"
"RULES:\n"
"1. approve if: change matches intent, no syntax errors, maintains document structure\n"
"2. reject if: wrong content, broken formatting, unrelated changes\n"
"3. NO prose, NO markdown code fences, ONLY the JSON object";

} // namespace docgen
