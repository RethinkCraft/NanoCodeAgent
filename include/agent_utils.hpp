#pragma once

#include <string>
#include <nlohmann/json.hpp>

// Truncate tool output unconditionally if it exceeds limits, and append a marker.
std::string truncate_tool_output(const std::string& output, size_t max_bytes);

// Slide window over messages to keep them under max_context_bytes limit.
// Will replace old tool outputs and user requests with placeholders if necessary.
void enforce_context_limits(nlohmann::json& messages, size_t max_context_bytes);
