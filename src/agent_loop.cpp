#include "agent_loop.hpp"
#include "agent_utils.hpp"
#include "agent_tools.hpp"
#include "tool_call.hpp"
#include "logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kFailedTestsJoinLimit = 3;
constexpr int kMaxFailureRetries = 1;
constexpr std::size_t kMaxFingerprintArgumentsBytes = 128;
constexpr std::size_t kCompactionRecentNonSystemMessages = 3;
constexpr std::size_t kCompactionSourceMaxBytes = 24 * 1024;
constexpr std::size_t kDelegateErrorPreviewBytes = 256;

enum class ToolFailureClass {
    None,
    Retryable,
    NeedsInspection,
    Fatal,
    Blocked,
};

struct ToolFailureAnalysis {
    bool is_failure = false;
    ToolFailureClass classification = ToolFailureClass::None;
    std::string guidance;
    std::string fingerprint;
};

struct DelegateRequest {
    std::string role;
    std::string task;
    std::vector<std::string> context_files;
    std::string context_notes;
    std::string expected_output;
    int max_turns = 0;
};

struct AgentSessionOptions {
    bool child_mode = false;
};

struct AgentSessionResult {
    nlohmann::json messages = nlohmann::json::array();
    bool completed = false;
    bool state_contaminated = false;
    std::string final_content;
};

const char* tool_failure_class_to_string(ToolFailureClass classification) {
    switch (classification) {
        case ToolFailureClass::None:
            return "none";
        case ToolFailureClass::Retryable:
            return "retryable";
        case ToolFailureClass::NeedsInspection:
            return "needs_inspection";
        case ToolFailureClass::Fatal:
            return "fatal";
        case ToolFailureClass::Blocked:
            return "blocked";
    }
    return "unknown";
}

std::string join_failed_tests(const nlohmann::json& failed_tests) {
    if (!failed_tests.is_array() || failed_tests.empty()) {
        return "";
    }

    std::string joined;
    const std::size_t limit = std::min<std::size_t>(failed_tests.size(), kFailedTestsJoinLimit);
    std::size_t appended_strings = 0;
    for (std::size_t i = 0; i < limit; ++i) {
        if (!failed_tests[i].is_string()) {
            continue;
        }
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += failed_tests[i].get<std::string>();
        ++appended_strings;
    }

    bool has_hidden_string = false;
    for (std::size_t i = limit; i < failed_tests.size(); ++i) {
        if (failed_tests[i].is_string()) {
            has_hidden_string = true;
            break;
        }
    }

    if (appended_strings > 0 && has_hidden_string) {
        joined += ", ...";
    }
    return joined;
}

struct FingerprintAccumulator {
    std::uint64_t hash = 14695981039346656037ull;
    std::size_t length = 0;
    std::string preview;

    void append_char(char ch) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ull;
        ++length;
        if (preview.size() < kMaxFingerprintArgumentsBytes) {
            preview.push_back(ch);
        }
    }

    void append_literal(const char* text) {
        for (const char* p = text; *p != '\0'; ++p) {
            append_char(*p);
        }
    }

    void append_string(const std::string& text) {
        for (char ch : text) {
            append_char(ch);
        }
    }
};

void append_json_fingerprint(const nlohmann::json& value, FingerprintAccumulator& accumulator);

void append_json_string_fingerprint(const std::string& value, FingerprintAccumulator& accumulator) {
    static constexpr char kHexDigits[] = "0123456789abcdef";

    accumulator.append_char('"');
    for (unsigned char ch : value) {
        switch (ch) {
            case '"':
                accumulator.append_literal("\\\"");
                break;
            case '\\':
                accumulator.append_literal("\\\\");
                break;
            case '\b':
                accumulator.append_literal("\\b");
                break;
            case '\f':
                accumulator.append_literal("\\f");
                break;
            case '\n':
                accumulator.append_literal("\\n");
                break;
            case '\r':
                accumulator.append_literal("\\r");
                break;
            case '\t':
                accumulator.append_literal("\\t");
                break;
            default:
                if (ch < 0x20) {
                    accumulator.append_literal("\\u00");
                    accumulator.append_char(kHexDigits[(ch >> 4) & 0x0F]);
                    accumulator.append_char(kHexDigits[ch & 0x0F]);
                } else {
                    accumulator.append_char(static_cast<char>(ch));
                }
                break;
        }
    }
    accumulator.append_char('"');
}

void append_json_fingerprint(const nlohmann::json& value, FingerprintAccumulator& accumulator) {
    switch (value.type()) {
        case nlohmann::json::value_t::null:
            accumulator.append_literal("null");
            return;
        case nlohmann::json::value_t::boolean:
            accumulator.append_literal(value.get<bool>() ? "true" : "false");
            return;
        case nlohmann::json::value_t::number_integer:
            accumulator.append_string(std::to_string(value.get<std::int64_t>()));
            return;
        case nlohmann::json::value_t::number_unsigned:
            accumulator.append_string(std::to_string(value.get<std::uint64_t>()));
            return;
        case nlohmann::json::value_t::number_float:
            accumulator.append_string(value.dump());
            return;
        case nlohmann::json::value_t::string:
            append_json_string_fingerprint(value.get_ref<const std::string&>(), accumulator);
            return;
        case nlohmann::json::value_t::array: {
            accumulator.append_char('[');
            bool first = true;
            for (const auto& item : value) {
                if (!first) {
                    accumulator.append_char(',');
                }
                append_json_fingerprint(item, accumulator);
                first = false;
            }
            accumulator.append_char(']');
            return;
        }
        case nlohmann::json::value_t::object: {
            accumulator.append_char('{');
            bool first = true;
            for (const auto& item : value.items()) {
                if (!first) {
                    accumulator.append_char(',');
                }
                append_json_string_fingerprint(item.key(), accumulator);
                accumulator.append_char(':');
                append_json_fingerprint(item.value(), accumulator);
                first = false;
            }
            accumulator.append_char('}');
            return;
        }
        case nlohmann::json::value_t::binary:
            accumulator.append_literal("<binary:");
            accumulator.append_string(std::to_string(value.get_binary().size()));
            accumulator.append_char('>');
            return;
        case nlohmann::json::value_t::discarded:
            accumulator.append_literal("<discarded>");
            return;
    }
}

std::string compact_arguments_fingerprint(const nlohmann::json& arguments) {
    FingerprintAccumulator accumulator;
    append_json_fingerprint(arguments, accumulator);

    if (accumulator.length <= kMaxFingerprintArgumentsBytes) {
        return accumulator.preview;
    }

    return "args[len=" + std::to_string(accumulator.length) + ",hash=" + std::to_string(accumulator.hash) + "]";
}

ToolFailureAnalysis make_failure_analysis(ToolFailureClass classification,
                                          std::string guidance,
                                          std::string fingerprint) {
    return {
        true,
        classification,
        std::move(guidance),
        std::move(fingerprint),
    };
}

ToolFailureAnalysis analyze_apply_patch_result(const ToolCall& tc,
                                               const nlohmann::json& result,
                                               bool ok) {
    if (ok) {
        return {};
    }

    const std::string reject_code = result.value("reject_code", "");
    const int patch_index = result.value("patch_index", -1);
    const std::string arguments_fingerprint = compact_arguments_fingerprint(tc.arguments);
    const std::string fingerprint = "apply_patch:" + reject_code + ":" + std::to_string(patch_index) +
                                    ":" + arguments_fingerprint;

    if (reject_code == "writeback_failure") {
        return make_failure_analysis(ToolFailureClass::Fatal, "", fingerprint);
    }

    if (reject_code == "no_match") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect the current file content and adjust old_text so it matches exactly once before retrying.",
            fingerprint);
    }
    if (reject_code == "multiple_matches") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect the file and narrow the patch target to a unique snippet before retrying.",
            fingerprint);
    }
    if (reject_code == "empty_old_text" || reject_code == "invalid_batch_entry") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: fix the patch arguments before retrying; this failure came from invalid patch input, not writeback.",
            fingerprint);
    }
    if (reject_code == "file_read_failure" || reject_code == "binary_file" || reject_code == "truncated_file") {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect the target path and file type before retrying; the patch did not reach writeback.",
            fingerprint);
    }

    return make_failure_analysis(ToolFailureClass::Fatal, "", fingerprint);
}

ToolFailureAnalysis analyze_build_test_result(const ToolCall& tc,
                                              const nlohmann::json& result,
                                              bool ok,
                                              bool timed_out,
                                              const std::string& status) {
    const std::string joined_failed_tests =
        join_failed_tests(result.value("failed_tests", nlohmann::json::array()));
    const std::string arguments_fingerprint = compact_arguments_fingerprint(tc.arguments);
    const std::string fingerprint = tc.name + ":" + status + ":" + std::to_string(result.value("exit_code", -1)) +
                                    ":" + (timed_out ? "timed_out" : "not_timed_out") + ":" + arguments_fingerprint +
                                    ":" + joined_failed_tests;

    if (ok && status != "timed_out") {
        return {};
    }

    if (timed_out || status == "timed_out") {
        return make_failure_analysis(
            ToolFailureClass::Retryable,
            "Recommended next action: inspect the timeout summary and either narrow the command scope or increase timeout_ms before retrying.",
            fingerprint);
    }

    if (status == "failed" || !ok) {
        if (tc.name == "test_project_safe") {
            std::string guidance =
                "Recommended next action: inspect summary/stdout/stderr and the reported failed tests before choosing the next fix.";
            if (!joined_failed_tests.empty()) {
                guidance += " Reported failed_tests: " + joined_failed_tests + ".";
            }
            return make_failure_analysis(
                ToolFailureClass::NeedsInspection,
                std::move(guidance),
                fingerprint);
        }

        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect summary/stdout/stderr to understand the build failure before changing code or retrying.",
            fingerprint);
    }

    return {};
}

std::string make_recovery_guidance_message(const ToolCall& tc, const ToolFailureAnalysis& analysis) {
    return "Tool recovery guidance for " + tc.name + ": classification=" +
           tool_failure_class_to_string(analysis.classification) + ". " + analysis.guidance;
}

std::string make_skipped_tool_output() {
    return nlohmann::json{
        {"ok", false},
        {"status", "skipped"},
        {"reason", "not executed due to prior tool failure"},
    }.dump();
}

void append_tool_message(nlohmann::json& messages,
                         const std::string& tool_call_id,
                         const std::string& name,
                         const std::string& content) {
    messages.push_back({
        {"role", "tool"},
        {"tool_call_id", tool_call_id},
        {"name", name},
        {"content", content},
    });
}

std::string preview_text(const std::string& text, std::size_t max_bytes) {
    if (text.size() <= max_bytes) {
        return text;
    }
    return text.substr(0, max_bytes) + "...";
}

std::size_t messages_size_bytes(const nlohmann::json& messages) {
    return messages.dump().size();
}

nlohmann::json filter_delegate_subagent_tool(const nlohmann::json& tools_registry) {
    if (!tools_registry.is_array()) {
        return nlohmann::json::array();
    }

    nlohmann::json filtered = nlohmann::json::array();
    for (const auto& tool : tools_registry) {
        if (!tool.is_object() || !tool.contains("function")) {
            continue;
        }
        if (tool["function"].value("name", "") == "delegate_subagent") {
            continue;
        }
        filtered.push_back(tool);
    }
    return filtered;
}

std::string build_compaction_source(const nlohmann::json& old_messages) {
    std::string source;
    source.reserve(std::min<std::size_t>(old_messages.dump().size(), kCompactionSourceMaxBytes));

    for (const auto& message : old_messages) {
        std::string block = "role=" + message.value("role", "");
        if (message.value("role", "") == "tool") {
            block += " tool=" + message.value("name", "");
            block += " tool_call_id=" + message.value("tool_call_id", "");
        }
        block += "\n";
        block += message.value("content", "");
        block += "\n\n";

        if (source.size() + block.size() > kCompactionSourceMaxBytes) {
            const std::size_t keep = kCompactionSourceMaxBytes > source.size() ?
                                     kCompactionSourceMaxBytes - source.size() : 0;
            source += block.substr(0, keep);
            source += "\n<OLDER HISTORY TRUNCATED FOR COMPACTION>";
            break;
        }
        source += block;
    }

    return source;
}

void compact_messages_if_needed(const AgentConfig& config,
                                nlohmann::json& messages,
                                LLMStreamFunc llm_func) {
    if (!messages.is_array() || messages.size() <= 2) {
        return;
    }
    if (config.max_context_bytes == 0 || messages_size_bytes(messages) <= config.max_context_bytes) {
        return;
    }
    


    std::vector<std::size_t> non_system_indices;
    non_system_indices.reserve(messages.size());
    for (std::size_t i = 1; i < messages.size(); ++i) {
        if (messages[i].value("role", "") != "system") {
            non_system_indices.push_back(i);
        }
    }
    


    if (non_system_indices.size() < kCompactionRecentNonSystemMessages) {
        LOG_INFO("Compaction skipped: not enough non-system messages");
        return;
    }

    // When non_system_indices.size() == kCompactionRecentNonSystemMessages,
    // recent_start_index will point to the first non-system message,
    // resulting in empty old_messages and compaction will be skipped.
    // This matches the intuitive behavior: no extra messages to compact.
    const std::size_t recent_start_index = non_system_indices[non_system_indices.size() - kCompactionRecentNonSystemMessages];
    LOG_INFO("Compaction: recent_start_index={} ({} recent non-system messages retained)",
             recent_start_index, kCompactionRecentNonSystemMessages);
    nlohmann::json old_messages = nlohmann::json::array();
    for (std::size_t i = 1; i < recent_start_index; ++i) {
        old_messages.push_back(messages[i]);
    }
    LOG_INFO("Compaction: old_messages size={}", old_messages.size());
    if (old_messages.empty()) {
        LOG_INFO("Compaction skipped: old_messages is empty");
        return;
    }

    const std::string source = build_compaction_source(old_messages);
    LOG_INFO("Compaction: source size={} bytes", source.size());
    if (source.empty()) {
        LOG_INFO("Compaction skipped: source is empty");
        return;
    }

    nlohmann::json compaction_messages = nlohmann::json::array({
        {
            {"role", "system"},
            {"content",
             "You are a context compactor for an autonomous coding agent. "
             "Return only a concise factual summary. Preserve important file paths, commands, decisions, and errors. "
             "Do not call tools."}
        },
        {
            {"role", "user"},
            {"content",
             "Summarize the older conversation history below so the agent can continue working.\n"
             "Keep the summary compact but preserve concrete facts, especially paths, commands, decisions, and errors.\n\n"
             + source}
        }
    });

    nlohmann::json response;
    try {
        response = llm_func(config, compaction_messages, nlohmann::json::array());
    } catch (const std::exception& e) {
        LOG_ERROR("Context compaction failed: {}", e.what());
        return;
    }

    const std::string summary = response.value("content", "");
    LOG_INFO("Compaction: received summary, size={} bytes", summary.size());
    if (summary.empty()) {
        LOG_INFO("Compaction skipped: summary is empty");
        return;
    }

    nlohmann::json compacted = nlohmann::json::array();
    compacted.push_back(messages[0]);
    compacted.push_back({
        {"role", "system"},
        {"content", "Compacted earlier context summary:\n" + summary}
    });
    for (std::size_t i = recent_start_index; i < messages.size(); ++i) {
        compacted.push_back(messages[i]);
    }

    size_t original_size = messages_size_bytes(messages);
    size_t compacted_size = messages_size_bytes(compacted);
    LOG_INFO("Compaction: original_size={}, compacted_size={}", original_size, compacted_size);
    if (compacted_size < original_size) {
        messages = std::move(compacted);
        LOG_INFO("Compaction applied: reduced size by {} bytes", original_size - compacted_size);
    } else {
        LOG_INFO("Compaction not applied: compacted size not smaller");
    }
}

bool parse_string_array_argument(const nlohmann::json& arguments,
                                 const char* key,
                                 std::vector<std::string>* out,
                                 std::string* error) {
    out->clear();
    if (!arguments.contains(key)) {
        return true;
    }
    if (!arguments.at(key).is_array()) {
        *error = "Argument '" + std::string(key) + "' must be an array of strings.";
        return false;
    }
    for (const auto& item : arguments.at(key)) {
        if (!item.is_string()) {
            *error = "Argument '" + std::string(key) + "' must be an array of strings.";
            return false;
        }
        out->push_back(item.get<std::string>());
    }
    return true;
}

bool parse_delegate_request(const ToolCall& tc, DelegateRequest* out, std::string* error) {
    const auto& arguments = tc.arguments;
    if (!arguments.is_object()) {
        *error = "delegate_subagent arguments must be a JSON object.";
        return false;
    }

    static const std::vector<std::string> kAllowedKeys{
        "role", "task", "context_files", "context_notes", "expected_output", "max_turns"};
    for (const auto& [key, _] : arguments.items()) {
        if (std::find(kAllowedKeys.begin(), kAllowedKeys.end(), key) == kAllowedKeys.end()) {
            *error = "Unknown argument '" + key + "' for delegate_subagent.";
            return false;
        }
    }

    if (!arguments.contains("role") || !arguments.at("role").is_string() ||
        arguments.at("role").get<std::string>().empty()) {
        *error = "Argument 'role' for delegate_subagent must be a non-empty string.";
        return false;
    }
    if (!arguments.contains("task") || !arguments.at("task").is_string() ||
        arguments.at("task").get<std::string>().empty()) {
        *error = "Argument 'task' for delegate_subagent must be a non-empty string.";
        return false;
    }

    out->role = arguments.at("role").get<std::string>();
    out->task = arguments.at("task").get<std::string>();
    out->context_notes = arguments.value("context_notes", "");
    out->expected_output = arguments.value("expected_output", "");
    out->max_turns = 0;

    if (arguments.contains("context_notes") && !arguments.at("context_notes").is_string()) {
        *error = "Argument 'context_notes' must be a string.";
        return false;
    }
    if (arguments.contains("expected_output") && !arguments.at("expected_output").is_string()) {
        *error = "Argument 'expected_output' must be a string.";
        return false;
    }
    if (!parse_string_array_argument(arguments, "context_files", &out->context_files, error)) {
        return false;
    }

    if (arguments.contains("max_turns")) {
        const auto& value = arguments.at("max_turns");
        if (!value.is_number_integer() && !value.is_number_unsigned()) {
            *error = "Argument 'max_turns' must be a positive integer.";
            return false;
        }
        out->max_turns = value.get<int>();
        if (out->max_turns <= 0) {
            *error = "Argument 'max_turns' must be a positive integer.";
            return false;
        }
    }

    return true;
}

nlohmann::json make_delegate_result_base(const DelegateRequest& request) {
    return nlohmann::json{
        {"ok", false},
        {"role", request.role},
        {"task", request.task},
        {"result_summary", ""},
        {"files_touched", nlohmann::json::array()},
        {"key_facts", nlohmann::json::array()},
        {"open_questions", nlohmann::json::array()},
        {"commands_ran", nlohmann::json::array()},
        {"verification_passed", false},
        {"error", ""}
    };
}

nlohmann::json make_delegate_error_result(const DelegateRequest& request, const std::string& error) {
    nlohmann::json result = make_delegate_result_base(request);
    result["error"] = error;
    return result;
}

bool normalize_string_array_field(const nlohmann::json& source,
                                  const char* key,
                                  nlohmann::json* destination,
                                  std::string* error) {
    if (!source.contains(key)) {
        (*destination)[key] = nlohmann::json::array();
        return true;
    }
    if (!source.at(key).is_array()) {
        *error = "Child summary field '" + std::string(key) + "' must be an array of strings.";
        return false;
    }
    nlohmann::json normalized = nlohmann::json::array();
    for (const auto& item : source.at(key)) {
        if (!item.is_string()) {
            *error = "Child summary field '" + std::string(key) + "' must be an array of strings.";
            return false;
        }
        normalized.push_back(item.get<std::string>());
    }
    (*destination)[key] = std::move(normalized);
    return true;
}

std::string build_child_system_prompt(const DelegateRequest& request) {
    return "You are a temporary delegated subagent for the role '" + request.role + "'. "
           "You are not the main orchestrator. Use only the provided tools for this narrow task. "
           "Do not call delegate_subagent. When you finish, return only a JSON object with these fields: "
           "ok, result_summary, files_touched, key_facts, open_questions, commands_ran, verification_passed, error.";
}

std::string build_child_user_prompt(const DelegateRequest& request) {
    std::ostringstream prompt;
    prompt << "Task:\n" << request.task << "\n\n";
    if (!request.context_files.empty()) {
        prompt << "Inspect these files first if relevant:\n";
        for (const auto& path : request.context_files) {
            prompt << "- " << path << "\n";
        }
        prompt << "\n";
    }
    if (!request.context_notes.empty()) {
        prompt << "Context notes:\n" << request.context_notes << "\n\n";
    }
    if (!request.expected_output.empty()) {
        prompt << "Expected output focus:\n" << request.expected_output << "\n\n";
    }
    prompt << "Return only the required JSON object. Keep it concise.";
    return prompt.str();
}

bool derive_stricter_child_limit(int parent_limit, int requested_limit, int* out_limit, std::string* error) {
    if (parent_limit <= 1) {
        *error = "Parent limits are too tight to create a strictly smaller child session.";
        return false;
    }

    int child_limit = std::max(1, parent_limit / 2);
    if (child_limit >= parent_limit) {
        child_limit = parent_limit - 1;
    }
    if (requested_limit > 0) {
        child_limit = std::min(child_limit, requested_limit);
    }
    if (child_limit <= 0 || child_limit >= parent_limit) {
        *error = "Unable to derive strictly tighter child limits from the current parent configuration.";
        return false;
    }

    *out_limit = child_limit;
    return true;
}

bool derive_stricter_child_size_limit(std::size_t parent_limit, std::size_t* out_limit, std::string* error) {
    if (parent_limit <= 1) {
        *error = "Parent byte limits are too tight to create a strictly smaller child session.";
        return false;
    }

    std::size_t child_limit = std::max<std::size_t>(1, parent_limit / 2);
    if (child_limit >= parent_limit) {
        child_limit = parent_limit - 1;
    }
    if (child_limit == 0 || child_limit >= parent_limit) {
        *error = "Unable to derive strictly tighter child byte limits from the current parent configuration.";
        return false;
    }

    *out_limit = child_limit;
    return true;
}

bool derive_child_config(const AgentConfig& parent,
                         const DelegateRequest& request,
                         AgentConfig* child,
                         std::string* error) {
    *child = parent;
    if (!derive_stricter_child_limit(parent.max_turns, request.max_turns, &child->max_turns, error)) {
        return false;
    }
    if (!derive_stricter_child_limit(parent.max_tool_calls_per_turn, 0, &child->max_tool_calls_per_turn, error)) {
        return false;
    }
    if (!derive_stricter_child_limit(parent.max_total_tool_calls, 0, &child->max_total_tool_calls, error)) {
        return false;
    }
    if (child->max_total_tool_calls < child->max_tool_calls_per_turn) {
        child->max_total_tool_calls = child->max_tool_calls_per_turn;
        if (child->max_total_tool_calls >= parent.max_total_tool_calls) {
            *error = "Unable to keep child total tool calls below the parent limit.";
            return false;
        }
    }
    if (!derive_stricter_child_size_limit(parent.max_tool_output_bytes, &child->max_tool_output_bytes, error)) {
        return false;
    }
    if (!derive_stricter_child_size_limit(parent.max_context_bytes, &child->max_context_bytes, error)) {
        return false;
    }
    return true;
}

ToolFailureAnalysis analyze_tool_result(const ToolCall& tc, const std::string& raw_output) {
    nlohmann::json result;
    try {
        result = nlohmann::json::parse(raw_output);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Tool result JSON parse failed for " + tc.name + ": " + e.what());
        return make_failure_analysis(
            ToolFailureClass::Fatal,
            "",
            "fatal:non_json:" + tc.name);
    }

    if (!result.is_object()) {
        return make_failure_analysis(
            ToolFailureClass::Fatal,
            "",
            "fatal:non_object:" + tc.name);
    }

    const bool ok = result.value("ok", true);
    const bool timed_out = result.value("timed_out", false);
    const std::string status = result.value("status", "");

    if (status == "blocked") {
        return make_failure_analysis(
            ToolFailureClass::Blocked,
            "Recommended next action: use an allowed read-only inspection tool or change approval settings outside the run. Do not repeat the blocked call unchanged.",
            "blocked:" + tc.name + ":" + compact_arguments_fingerprint(tc.arguments));
    }

    if (tc.name == "apply_patch") {
        return analyze_apply_patch_result(tc, result, ok);
    }

    if (tc.name == "build_project_safe" || tc.name == "test_project_safe") {
        return analyze_build_test_result(tc, result, ok, timed_out, status);
    }

    if (tc.name == "delegate_subagent" && !ok) {
        return make_failure_analysis(
            ToolFailureClass::NeedsInspection,
            "Recommended next action: inspect the structured child error summary and decide whether to retry the delegated task or continue locally.",
            "delegate_subagent:" + compact_arguments_fingerprint(tc.arguments));
    }

    if (!ok || timed_out || status == "failed" || status == "timed_out") {
        return make_failure_analysis(
            ToolFailureClass::Fatal,
            "",
            "fatal:" + tc.name + ":" + compact_arguments_fingerprint(tc.arguments));
    }

    return {};
}

AgentSessionResult run_agent_session(const AgentConfig& config,
                                     const std::string& system_prompt,
                                     const std::string& user_prompt,
                                     const nlohmann::json& tools_registry,
                                     LLMStreamFunc llm_func,
                                     const AgentSessionOptions& options);

nlohmann::json run_subagent(const ToolCall& tc,
                           const AgentConfig& parent_config,
                           const nlohmann::json& parent_tools_registry,
                           LLMStreamFunc llm_func) {
    DelegateRequest request;
    std::string error;
    if (!parse_delegate_request(tc, &request, &error)) {
        DelegateRequest fallback;
        if (tc.arguments.is_object()) {
            fallback.role = tc.arguments.value("role", "");
            fallback.task = tc.arguments.value("task", "");
        }
        return make_delegate_error_result(fallback, error);
    }

    AgentConfig child_config;
    if (!derive_child_config(parent_config, request, &child_config, &error)) {
        return make_delegate_error_result(request, error);
    }

    const nlohmann::json child_tools = filter_delegate_subagent_tool(parent_tools_registry);
    AgentSessionResult child_result = run_agent_session(
        child_config,
        build_child_system_prompt(request),
        build_child_user_prompt(request),
        child_tools,
        llm_func,
        AgentSessionOptions{.child_mode = true});

    if (!child_result.completed) {
        return make_delegate_error_result(request, "Child run ended without a final summary.");
    }

    if (child_result.final_content.empty()) {
        return make_delegate_error_result(request, "Child run finished without returning a summary payload.");
    }

    nlohmann::json parsed_summary;
    try {
        parsed_summary = nlohmann::json::parse(child_result.final_content);
    } catch (const std::exception& e) {
        return make_delegate_error_result(
            request,
            "Child summary was not valid JSON: " + std::string(e.what()) +
                ". Raw preview: " + preview_text(child_result.final_content, kDelegateErrorPreviewBytes));
    }

    if (!parsed_summary.is_object()) {
        return make_delegate_error_result(request, "Child summary must be a JSON object.");
    }

    nlohmann::json normalized = make_delegate_result_base(request);
    normalized["ok"] = parsed_summary.value("ok", true);
    normalized["result_summary"] = parsed_summary.value("result_summary", "");
    normalized["verification_passed"] = parsed_summary.value("verification_passed", false);
    normalized["error"] = parsed_summary.value("error", "");

    if (!normalized["result_summary"].is_string()) {
        return make_delegate_error_result(request, "Child summary field 'result_summary' must be a string.");
    }
    if (!parsed_summary.contains("verification_passed") || !parsed_summary.at("verification_passed").is_boolean()) {
        normalized["verification_passed"] = false;
    }
    if (!normalized["error"].is_string()) {
        return make_delegate_error_result(request, "Child summary field 'error' must be a string.");
    }
    if (!normalize_string_array_field(parsed_summary, "files_touched", &normalized, &error) ||
        !normalize_string_array_field(parsed_summary, "key_facts", &normalized, &error) ||
        !normalize_string_array_field(parsed_summary, "open_questions", &normalized, &error) ||
        !normalize_string_array_field(parsed_summary, "commands_ran", &normalized, &error)) {
        return make_delegate_error_result(request, error);
    }

    if (!normalized["ok"].get<bool>() && normalized["error"].get<std::string>().empty()) {
        normalized["error"] = "Child reported failure without an error message.";
    }
    return normalized;
}

AgentSessionResult run_agent_session(const AgentConfig& config,
                                     const std::string& system_prompt,
                                     const std::string& user_prompt,
                                     const nlohmann::json& tools_registry,
                                     LLMStreamFunc llm_func,
                                     const AgentSessionOptions& options) {
    AgentSessionResult session;

    if (!system_prompt.empty()) {
        session.messages.push_back({{"role", "system"}, {"content", system_prompt}});
    }
    session.messages.push_back({{"role", "user"}, {"content", user_prompt}});

    int turns = 0;
    int total_tool_calls = 0;
    std::unordered_map<std::string, int> failure_retry_counts;

    while (true) {
        turns++;
        if (turns > config.max_turns) {
            LOG_ERROR("Broker loop hit max_turns (" + std::to_string(config.max_turns) + "), aborting to prevent infinite loop.");
            if (!options.child_mode) {
                std::cerr << "[Agent Error] Exceeded max standard turns.\n";
            }
            break;
        }

        compact_messages_if_needed(config, session.messages, llm_func);
        enforce_context_limits(session.messages, config.max_context_bytes);

        LOG_INFO("Agent Turn " + std::to_string(turns) + " started...");

        nlohmann::json response_message;
        try {
            response_message = llm_func(config, session.messages, tools_registry);
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("LLM Execution failed: ") + e.what());
            if (!options.child_mode) {
                std::cerr << "[Agent Error] LLM request failed: " << e.what() << "\n";
            }
            break;
        }

        session.messages.push_back(response_message);

        if (!response_message.contains("tool_calls") || response_message["tool_calls"].empty()) {
            LOG_INFO("No tool calls. Agent loop complete.");
            session.completed = true;
            session.final_content = response_message.value("content", "");
            if (!options.child_mode) {
                std::cout << "[Agent Complete] " << session.final_content << "\n";
            }
            break;
        }

        auto tool_calls = response_message["tool_calls"];
        if (tool_calls.size() > config.max_tool_calls_per_turn) {
            LOG_ERROR("Too many tools requested in turn: " + std::to_string(tool_calls.size()));
            if (!options.child_mode) {
                std::cerr << "[Agent Error] Tool flood detected, limit " << config.max_tool_calls_per_turn << ". Aborting.\n";
            }
            break;
        }

        total_tool_calls += static_cast<int>(tool_calls.size());
        if (total_tool_calls > config.max_total_tool_calls) {
            LOG_ERROR("Max total tool calls exceeded: " + std::to_string(total_tool_calls));
            if (!options.child_mode) {
                std::cerr << "[Agent Error] Global tool call limit hit, aborting.\n";
            }
            break;
        }

        bool state_contaminated = false;

        for (std::size_t tool_index = 0; tool_index < tool_calls.size(); ++tool_index) {
            const auto& tc_json = tool_calls[tool_index];
            ToolCall tc;
            tc.id = tc_json.value("id", "");
            if (tc_json.contains("function")) {
                tc.name = tc_json["function"].value("name", "");
                auto raw_args = tc_json["function"].value("arguments", "{}");
                try {
                    tc.arguments = nlohmann::json::parse(raw_args);
                } catch (...) {
                    LOG_ERROR("Tool JSON parse failed for " + tc.name);
                    tc.arguments = nlohmann::json::object();
                }
            }

            std::string raw_output;
            if (tc.name == "delegate_subagent") {
                if (options.child_mode) {
                    raw_output = nlohmann::json{
                        {"ok", false},
                        {"status", "failed"},
                        {"error", "delegate_subagent is disabled inside child sessions."}
                    }.dump();
                    append_tool_message(session.messages, tc.id, tc.name, raw_output);
                    state_contaminated = true;
                    break;
                }
                raw_output = run_subagent(tc, config, tools_registry, llm_func).dump();
            } else {
                raw_output = execute_tool(tc, config);
            }

            const ToolFailureAnalysis analysis = analyze_tool_result(tc, raw_output);
            std::string output = truncate_tool_output(raw_output, config.max_tool_output_bytes);
            append_tool_message(session.messages, tc.id, tc.name, output);

            if (!analysis.is_failure) {
                continue;
            }

            if (analysis.classification == ToolFailureClass::Fatal) {
                LOG_ERROR("Tool failed fatally: " + output);
                if (!options.child_mode) {
                    std::cerr << "[Agent Error] Tool " << tc.name << " failed fatally: " << output << "\n";
                }
                state_contaminated = true;
                break;
            }

            const int seen_count = ++failure_retry_counts[analysis.fingerprint];
            if (seen_count > kMaxFailureRetries) {
                LOG_ERROR("Tool failure repeated beyond retry budget: " + output);
                if (!options.child_mode) {
                    std::cerr << "[Agent Error] Tool " << tc.name
                              << " repeated the same recoverable failure and exceeded the retry budget.\n";
                }
                state_contaminated = true;
                break;
            }

            LOG_ERROR("Tool failed but is recoverable: " + output);
            session.messages.push_back({
                {"role", "system"},
                {"content", make_recovery_guidance_message(tc, analysis)}
            });
            for (std::size_t skipped_index = tool_index + 1; skipped_index < tool_calls.size(); ++skipped_index) {
                const auto& skipped_tc_json = tool_calls[skipped_index];
                const std::string skipped_tool_call_id = skipped_tc_json.value("id", "");
                std::string skipped_name;
                if (skipped_tc_json.contains("function")) {
                    skipped_name = skipped_tc_json["function"].value("name", "");
                }
                append_tool_message(session.messages, skipped_tool_call_id, skipped_name, make_skipped_tool_output());
            }
            break;
        }

        if (state_contaminated) {
            session.state_contaminated = true;
            if (!options.child_mode) {
                std::cerr << "[Agent Error] Run stopped due to state contamination (tool failure or timeout).\n";
            }
            break;
        }
    }

    return session;
}

}  // namespace

void agent_run(const AgentConfig& config,
               const std::string& system_prompt,
               const std::string& user_prompt,
               const nlohmann::json& tools_registry,
               LLMStreamFunc llm_func) {
    run_agent_session(config, system_prompt, user_prompt, tools_registry, llm_func, AgentSessionOptions{});
}
