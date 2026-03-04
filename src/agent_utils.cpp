#include "agent_utils.hpp"
#include <iostream>

std::string truncate_tool_output(const std::string& output, size_t max_bytes) {
    if (output.size() <= max_bytes) {
        return output;
    }
    std::string truncated = output.substr(0, max_bytes);
    truncated += "\n<TRUNCATED: original=" + std::to_string(output.size()) + 
                 " bytes, kept=" + std::to_string(max_bytes) + " bytes>";
    return truncated;
}

void enforce_context_limits(nlohmann::json& messages, size_t max_context_bytes) {
    if (!messages.is_array()) return;
    
    auto get_size = [&]() -> size_t {
        return messages.dump().size();
    };

    while (get_size() > max_context_bytes && messages.size() > 2) { // keep system + at least one message
        bool dropped = false;
        // Find the oldest tool message
        for (size_t i = 1; i < messages.size(); ++i) { // skip system
            if (messages[i].value("role", "") == "tool" && messages[i].value("content", "") != "<DROPPED old tool outputs to fit context>") {
                messages[i]["content"] = "<DROPPED old tool outputs to fit context>";
                dropped = true;
                break;
            }
        }
        
        // If no tool message left to drop, find the oldest user or assistant message to drop content
        if (!dropped) {
            for (size_t i = 1; i < messages.size(); ++i) {
                if (messages[i].value("content", "") != "<DROPPED old tool outputs to fit context>" && messages[i].value("content", "") != "") {
                    messages[i]["content"] = "<DROPPED old tool outputs to fit context>";
                    dropped = true;
                    break;
                }
            }
        }
        
        if (!dropped) {
            break; // Can't shrink further
        }
    }
}
