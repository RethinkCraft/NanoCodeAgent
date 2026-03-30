#include "docgen_pipeline.hpp"
#include "logger.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>

namespace docgen {

namespace fs = std::filesystem;

// Helper function matching apply_patch.cpp's behavior
static int count_occurrences_with_overlap(const std::string& haystack,
                                          const std::string& needle) {
    if (needle.empty()) {
        return 0;
    }
    int count = 0;
    std::string::size_type pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += 1;  // advance by 1, NOT by needle.size(), to detect overlaps
    }
    return count;
}

DocgenPipeline::DocgenPipeline(const AgentConfig& cfg, const SubagentContext& ctx)
    : config_(cfg), ctx_(ctx), llm_(std::make_unique<LlmClient>(cfg, ctx)) {}

void DocgenPipeline::set_target_files(std::vector<std::string> files) {
    target_files_ = std::move(files);
}

void DocgenPipeline::set_on_progress(std::function<void(const std::string&, const std::string&)> cb) {
    on_progress_ = std::move(cb);
}

std::expected<PipelineResult, std::string> DocgenPipeline::run(const std::string& diff_text) {
    PipelineResult result;
    
    if (on_progress_) on_progress_("analyze", "parsing diff");
    
    auto changes = analyze_changes(diff_text);
    if (!changes) {
        return std::unexpected(changes.error());
    }
    
    if (changes->affected_files.empty()) {
        result.success = true;
        return result;
    }
    
    for (const auto& target_file : target_files_) {
        if (on_progress_) on_progress_("locate", target_file);
        
        auto outline = extract_outline(target_file);
        if (!outline) continue;
        
        auto content = read_file_content(target_file);
        if (content.empty()) continue;
        
        auto locations = locate_targets(content, *outline, *changes);
        if (!locations || locations->locations.empty()) continue;
        
        for (const auto& loc : locations->locations) {
            if (on_progress_) on_progress_("patch", 
                target_file + ":" + std::to_string(loc.start_line));
            
            auto original = read_lines(content, loc.start_line, loc.end_line);
            auto patch_result = write_patches(original, changes->intent_summary);
            if (!patch_result || patch_result->patches.empty()) continue;
            
            auto after = original;
            int patches_skipped = 0;
            for (const auto& p : patch_result->patches) {
                // Only process known patch actions
                if (p.action != PatchAction::Replace && p.action != PatchAction::Delete &&
                    p.action != PatchAction::InsertBefore && p.action != PatchAction::InsertAfter) {
                    patches_skipped++;
                    continue;
                }
                
                // Check that old_text appears exactly once (as required by apply_patch_single)
                const int occurrences = count_occurrences_with_overlap(after, p.old_text);
                if (occurrences != 1) {
                    patches_skipped++;
                    continue;
                }
                
                const std::string::size_type pos = after.find(p.old_text);
                if (pos == std::string::npos) {
                    // Should not happen given occurrences == 1, but guard anyway
                    patches_skipped++;
                    continue;
                }
                
                switch (p.action) {
                    case PatchAction::Replace:
                        after.replace(pos, p.old_text.length(), p.new_text);
                        break;
                    case PatchAction::Delete:
                        after.replace(pos, p.old_text.length(), "");
                        break;
                    case PatchAction::InsertBefore:
                        after.replace(pos, 0, p.new_text);
                        break;
                    case PatchAction::InsertAfter:
                        after.replace(pos + p.old_text.length(), 0, p.new_text);
                        break;
                    default:
                        patches_skipped++;
                        break;
                }
            }
            
            if (patches_skipped > 0) {
                result.patches_rejected += patches_skipped;
            }
            
            // Skip review if all patches were rejected during simulation
            if (patches_skipped == static_cast<int>(patch_result->patches.size())) {
                continue;
            }
            
            auto review = review_patch(original, after);
            if (!review || review->verdict == ReviewVerdict::Reject) {
                // Only count patches that passed simulation but failed review
                result.patches_rejected += static_cast<int>(patch_result->patches.size()) - patches_skipped;
                continue;
            }
            
            auto entries = to_patch_entries(*patch_result, original);
            if (apply_patches(target_file, entries)) {
                result.patches_applied += static_cast<int>(entries.size());
            }
        }
    }
    
    result.success = true;
    return result;
}

std::expected<ChangeAnalystResult, std::string> DocgenPipeline::analyze_changes(const std::string& diff) {
    nlohmann::json user_ctx = {{"diff", diff}};
    
    auto resp = llm_->call_json(std::string(kChangeAnalystPrompt), user_ctx);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    
    try {
        return resp->get<ChangeAnalystResult>();
    } catch (const std::exception& e) {
        return std::unexpected(std::string("JSON parse error: ") + e.what());
    }
}

std::expected<ContextRouterResult, std::string> DocgenPipeline::locate_targets(
    const std::string& doc_content,
    const DocOutline& outline,
    const ChangeAnalystResult& changes
) {
    nlohmann::json user_ctx = {
        {"doc_outline", outline},
        {"intent_summary", changes.intent_summary}
    };
    
    auto resp = llm_->call_json(std::string(kContextRouterPrompt), user_ctx);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    
    try {
        return resp->get<ContextRouterResult>();
    } catch (const std::exception& e) {
        return std::unexpected(std::string("JSON parse error: ") + e.what());
    }
}

std::expected<PatchWriterResult, std::string> DocgenPipeline::write_patches(
    const std::string& original_text,
    const std::string& intent
) {
    std::string numbered;
    int line_num = 1;
    std::istringstream iss(original_text);
    for (std::string line; std::getline(iss, line); ++line_num) {
        numbered += std::to_string(line_num) + ": " + line + "\n";
    }
    
    nlohmann::json user_ctx = {
        {"original_text", numbered},
        {"intent_summary", intent}
    };
    
    auto resp = llm_->call_json(std::string(kPatchWriterPrompt), user_ctx);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    
    try {
        return resp->get<PatchWriterResult>();
    } catch (const std::exception& e) {
        return std::unexpected(std::string("JSON parse error: ") + e.what());
    }
}

std::expected<MicroReviewResult, std::string> DocgenPipeline::review_patch(
    const std::string& before,
    const std::string& after
) {
    nlohmann::json user_ctx = {
        {"before", before},
        {"after", after}
    };
    
    auto resp = llm_->call_json(std::string(kMicroReviewerPrompt), user_ctx);
    if (!resp) {
        return std::unexpected(resp.error());
    }
    
    try {
        return resp->get<MicroReviewResult>();
    } catch (const std::exception& e) {
        return std::unexpected(std::string("JSON parse error: ") + e.what());
    }
}

std::optional<DocOutline> DocgenPipeline::extract_outline(const std::string& file_path) {
    auto content = read_file_content(file_path);
    if (content.empty()) return std::nullopt;
    
    auto outline_result = parse_markdown_outline(content);
    if (!outline_result) return std::nullopt;
    
    outline_result->file_path = file_path;
    return *outline_result;
}

std::string DocgenPipeline::read_file_content(const std::string& rel_path) {
    fs::path full_path = fs::path(config_.workspace_abs) / rel_path;
    
    std::ifstream f(full_path, std::ios::binary);
    if (!f) return {};
    
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string DocgenPipeline::read_lines(const std::string& content, int start, int end) {
    if (start < 1 || end < start) return {};
    
    std::istringstream iss(content);
    std::string result;
    std::string line;
    int line_num = 1;
    
    while (std::getline(iss, line)) {
        if (line_num >= start && line_num <= end) {
            result += line + "\n";
        }
        if (line_num > end) break;
        ++line_num;
    }
    
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    
    return result;
}

std::vector<PatchEntry> DocgenPipeline::to_patch_entries(
    const PatchWriterResult& patches,
    const std::string& original
) {
    std::vector<PatchEntry> entries;
    
    for (const auto& p : patches.patches) {
        switch (p.action) {
            case PatchAction::Replace:
            case PatchAction::Delete:
                entries.push_back({p.old_text, p.new_text});
                break;
            case PatchAction::InsertBefore:
                // Insert before old_text: replace old_text with new_text + old_text
                entries.push_back({p.old_text, p.new_text + p.old_text});
                break;
            case PatchAction::InsertAfter:
                // Insert after old_text: replace old_text with old_text + new_text
                entries.push_back({p.old_text, p.old_text + p.new_text});
                break;
            default:
                // Unknown action – ignore with a log warning
                spdlog::warn("to_patch_entries: ignoring unknown patch action");
                break;
        }
    }
    
    return entries;
}

bool DocgenPipeline::apply_patches(const std::string& rel_path, const std::vector<PatchEntry>& entries) {
    if (entries.empty()) return true;
    
    auto result = apply_patch_batch(config_.workspace_abs, rel_path, entries);
    if (!result.ok) {
        LOG_ERROR("apply_patch_batch failed: {}", result.err);
    }
    return result.ok;
}

std::expected<DocOutline, std::string> parse_markdown_outline(const std::string& content) {
    DocOutline outline;
    std::regex heading_re(R"(^(#{1,6})\s+(.+)$)", std::regex::multiline);
    
    std::istringstream iss(content);
    std::string line;
    int line_num = 1;
    
    while (std::getline(iss, line)) {
        std::smatch match;
        if (std::regex_search(line, match, heading_re)) {
            int level = static_cast<int>(match[1].str().length());
            std::string text = match[2].str();
            outline.headings.push_back({level, line_num, text});
        }
        ++line_num;
    }
    
    return outline;
}

} // namespace docgen
