#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace docgen {

struct AffectedFile {
    std::string path;
    std::string change_type;
    std::optional<std::string> old_path;
    std::vector<std::string> public_symbols;
};

struct ChangeAnalystResult {
    std::vector<AffectedFile> affected_files;
    std::string intent_summary;
};

inline void to_json(nlohmann::json& j, const AffectedFile& f) {
    j = nlohmann::json{{"path", f.path}, {"change_type", f.change_type}};
    if (f.old_path) j["old_path"] = *f.old_path;
    if (!f.public_symbols.empty()) j["public_symbols"] = f.public_symbols;
}

inline void from_json(const nlohmann::json& j, AffectedFile& f) {
    j.at("path").get_to(f.path);
    j.at("change_type").get_to(f.change_type);
    if (j.contains("old_path")) f.old_path = j["old_path"].get<std::string>();
    if (j.contains("public_symbols")) f.public_symbols = j["public_symbols"].get<std::vector<std::string>>();
}

inline void to_json(nlohmann::json& j, const ChangeAnalystResult& r) {
    j = nlohmann::json{{"affected_files", r.affected_files}, {"intent_summary", r.intent_summary}};
}

inline void from_json(const nlohmann::json& j, ChangeAnalystResult& r) {
    j.at("affected_files").get_to(r.affected_files);
    j.at("intent_summary").get_to(r.intent_summary);
}

struct DocLocation {
    std::string target_file;
    int start_line = 0;
    int end_line = 0;
    std::optional<std::string> section_heading;
};

struct ContextRouterResult {
    std::vector<DocLocation> locations;
};

inline void to_json(nlohmann::json& j, const DocLocation& l) {
    j = nlohmann::json{{"target_file", l.target_file}, {"start_line", l.start_line}, {"end_line", l.end_line}};
    if (l.section_heading) j["section_heading"] = *l.section_heading;
}

inline void from_json(const nlohmann::json& j, DocLocation& l) {
    j.at("target_file").get_to(l.target_file);
    j.at("start_line").get_to(l.start_line);
    j.at("end_line").get_to(l.end_line);
    if (j.contains("section_heading")) l.section_heading = j["section_heading"].get<std::string>();
}

inline void to_json(nlohmann::json& j, const ContextRouterResult& r) {
    j = nlohmann::json{{"locations", r.locations}};
}

inline void from_json(const nlohmann::json& j, ContextRouterResult& r) {
    j.at("locations").get_to(r.locations);
}

enum class PatchAction { Replace, InsertBefore, InsertAfter, Delete };

inline std::string patch_action_to_string(PatchAction a) {
    switch (a) {
        case PatchAction::Replace: return "replace";
        case PatchAction::InsertBefore: return "insert_before";
        case PatchAction::InsertAfter: return "insert_after";
        case PatchAction::Delete: return "delete";
    }
    return "replace";
}

inline std::optional<PatchAction> patch_action_from_string(const std::string& s) {
    if (s == "replace") return PatchAction::Replace;
    if (s == "insert_before") return PatchAction::InsertBefore;
    if (s == "insert_after") return PatchAction::InsertAfter;
    if (s == "delete") return PatchAction::Delete;
    return std::nullopt;
}

struct TextPatch {
    PatchAction action = PatchAction::Replace;
    std::string old_text;
    std::string new_text;
    std::optional<int> line_hint;
};

struct PatchWriterResult {
    std::vector<TextPatch> patches;
    std::optional<std::string> rationale;
};

inline void to_json(nlohmann::json& j, const TextPatch& p) {
    j = nlohmann::json{{"action", patch_action_to_string(p.action)}, {"old_text", p.old_text}, {"new_text", p.new_text}};
    if (p.line_hint) j["line_hint"] = *p.line_hint;
}

inline void from_json(const nlohmann::json& j, TextPatch& p) {
    auto act = j.at("action").get<std::string>();
    auto action_opt = patch_action_from_string(act);
    if (!action_opt) {
        throw std::runtime_error("Invalid patch action: " + act);
    }
    p.action = *action_opt;
    if (j.contains("old_text")) j["old_text"].get_to(p.old_text);
    if (j.contains("new_text")) j["new_text"].get_to(p.new_text);
    if (j.contains("line_hint")) p.line_hint = j["line_hint"].get<int>();
}

inline void to_json(nlohmann::json& j, const PatchWriterResult& r) {
    j = nlohmann::json{{"patches", r.patches}};
    if (r.rationale) j["rationale"] = *r.rationale;
}

inline void from_json(const nlohmann::json& j, PatchWriterResult& r) {
    j.at("patches").get_to(r.patches);
    if (j.contains("rationale")) r.rationale = j["rationale"].get<std::string>();
}

enum class ReviewVerdict { Approve, Reject };

struct MicroReviewResult {
    ReviewVerdict verdict = ReviewVerdict::Approve;
    std::string reason;
    std::vector<std::string> issues;
};

inline void to_json(nlohmann::json& j, const MicroReviewResult& r) {
    j = nlohmann::json{{"verdict", r.verdict == ReviewVerdict::Approve ? "approve" : "reject"}, {"reason", r.reason}};
    if (!r.issues.empty()) j["issues"] = r.issues;
}

inline void from_json(const nlohmann::json& j, MicroReviewResult& r) {
    auto v = j.at("verdict").get<std::string>();
    r.verdict = (v == "approve") ? ReviewVerdict::Approve : ReviewVerdict::Reject;
    j.at("reason").get_to(r.reason);
    if (j.contains("issues")) j["issues"].get_to(r.issues);
}

struct DocOutline {
    std::string file_path;
    struct Heading {
        int level;
        int line_number;
        std::string text;
    };
    std::vector<Heading> headings;
};

inline void to_json(nlohmann::json& j, const DocOutline::Heading& h) {
    j = nlohmann::json{{"level", h.level}, {"line_number", h.line_number}, {"text", h.text}};
}

inline void from_json(const nlohmann::json& j, DocOutline::Heading& h) {
    j.at("level").get_to(h.level);
    j.at("line_number").get_to(h.line_number);
    j.at("text").get_to(h.text);
}

inline void to_json(nlohmann::json& j, const DocOutline& o) {
    j = nlohmann::json{{"file_path", o.file_path}, {"headings", o.headings}};
}

inline void from_json(const nlohmann::json& j, DocOutline& o) {
    j.at("file_path").get_to(o.file_path);
    j.at("headings").get_to(o.headings);
}

struct SubagentContext {
    int max_input_bytes = 16000;
    int timeout_ms = 60000;
    int max_retries = 2;
};

} // namespace docgen
