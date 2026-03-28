#pragma once

#include "config.hpp"
#include "docgen_types.hpp"
#include "docgen_llm.hpp"
#include "apply_patch.hpp"
#include <expected>
#include <functional>
#include <string>

namespace docgen {

struct PipelineResult {
    bool success = false;
    std::string error;
    int patches_applied = 0;
    int patches_rejected = 0;
};

class DocgenPipeline {
public:
    DocgenPipeline(const AgentConfig& cfg, const SubagentContext& ctx = {});
    
    std::expected<PipelineResult, std::string> run(const std::string& diff_text);

    void set_target_files(std::vector<std::string> files);
    void set_on_progress(std::function<void(const std::string& stage, const std::string& status)> cb);

private:
    std::expected<ChangeAnalystResult, std::string> analyze_changes(const std::string& diff);
    std::expected<ContextRouterResult, std::string> locate_targets(
        const std::string& doc_content,
        const DocOutline& outline,
        const ChangeAnalystResult& changes
    );
    std::expected<PatchWriterResult, std::string> write_patches(
        const std::string& original_text,
        const std::string& intent
    );
    std::expected<MicroReviewResult, std::string> review_patch(
        const std::string& before,
        const std::string& after
    );
    
    std::optional<DocOutline> extract_outline(const std::string& file_path);
    std::string read_file_content(const std::string& rel_path);
    std::string read_lines(const std::string& content, int start, int end);
    
    std::vector<PatchEntry> to_patch_entries(const PatchWriterResult& patches, const std::string& original);
    bool apply_patches(const std::string& rel_path, const std::vector<PatchEntry>& entries);

    AgentConfig config_;
    SubagentContext ctx_;
    std::unique_ptr<LlmClient> llm_;
    std::vector<std::string> target_files_{"README.md"};
    std::function<void(const std::string&, const std::string&)> on_progress_;
};

std::expected<DocOutline, std::string> parse_markdown_outline(const std::string& content);

} // namespace docgen
