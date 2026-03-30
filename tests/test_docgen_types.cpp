#include <gtest/gtest.h>
#include "docgen_types.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class DocgenTypesTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(DocgenTypesTest, AffectedFile_Serialization) {
    docgen::AffectedFile f;
    f.path = "src/main.cpp";
    f.change_type = "modify";
    f.public_symbols = {"main", "run"};
    
    json j = f;
    EXPECT_EQ(j["path"], "src/main.cpp");
    EXPECT_EQ(j["change_type"], "modify");
    EXPECT_TRUE(j.contains("public_symbols"));
    EXPECT_EQ(j["public_symbols"].size(), 2);
    
    auto f2 = j.get<docgen::AffectedFile>();
    EXPECT_EQ(f2.path, f.path);
    EXPECT_EQ(f2.change_type, f.change_type);
    EXPECT_EQ(f2.public_symbols, f.public_symbols);
    EXPECT_FALSE(f2.old_path.has_value());
}

TEST_F(DocgenTypesTest, AffectedFile_WithOldPath) {
    docgen::AffectedFile f;
    f.path = "src/new.cpp";
    f.change_type = "rename";
    f.old_path = "src/old.cpp";
    
    json j = f;
    EXPECT_TRUE(j.contains("old_path"));
    EXPECT_EQ(j["old_path"], "src/old.cpp");
    
    auto f2 = j.get<docgen::AffectedFile>();
    EXPECT_TRUE(f2.old_path.has_value());
    EXPECT_EQ(*f2.old_path, "src/old.cpp");
}

TEST_F(DocgenTypesTest, ChangeAnalystResult_Serialization) {
    docgen::ChangeAnalystResult r;
    r.affected_files.push_back({"src/a.cpp", "add", std::nullopt, {}});
    r.affected_files.push_back({"src/b.cpp", "delete", std::nullopt, {"func1"}});
    r.intent_summary = "Add new feature X";
    
    json j = r;
    EXPECT_EQ(j["affected_files"].size(), 2);
    EXPECT_EQ(j["intent_summary"], "Add new feature X");
    
    auto r2 = j.get<docgen::ChangeAnalystResult>();
    EXPECT_EQ(r2.affected_files.size(), 2);
    EXPECT_EQ(r2.intent_summary, r.intent_summary);
}

TEST_F(DocgenTypesTest, DocLocation_Serialization) {
    docgen::DocLocation loc;
    loc.target_file = "README.md";
    loc.start_line = 10;
    loc.end_line = 20;
    loc.section_heading = "Installation";
    
    json j = loc;
    EXPECT_EQ(j["target_file"], "README.md");
    EXPECT_EQ(j["start_line"], 10);
    EXPECT_EQ(j["end_line"], 20);
    EXPECT_EQ(j["section_heading"], "Installation");
    
    auto loc2 = j.get<docgen::DocLocation>();
    EXPECT_EQ(loc2.target_file, loc.target_file);
    EXPECT_EQ(loc2.start_line, loc.start_line);
    EXPECT_EQ(loc2.end_line, loc.end_line);
    EXPECT_TRUE(loc2.section_heading.has_value());
}

TEST_F(DocgenTypesTest, ContextRouterResult_Serialization) {
    docgen::ContextRouterResult r;
    r.locations.push_back({"README.md", 1, 10, "Header"});
    r.locations.push_back({"docs/guide.md", 50, 60, std::nullopt});
    
    json j = r;
    EXPECT_EQ(j["locations"].size(), 2);
    
    auto r2 = j.get<docgen::ContextRouterResult>();
    EXPECT_EQ(r2.locations.size(), 2);
    EXPECT_EQ(r2.locations[0].section_heading.value(), "Header");
    EXPECT_FALSE(r2.locations[1].section_heading.has_value());
}

TEST_F(DocgenTypesTest, PatchAction_Conversion) {
    EXPECT_EQ(docgen::patch_action_to_string(docgen::PatchAction::Replace), "replace");
    EXPECT_EQ(docgen::patch_action_to_string(docgen::PatchAction::InsertBefore), "insert_before");
    EXPECT_EQ(docgen::patch_action_to_string(docgen::PatchAction::InsertAfter), "insert_after");
    EXPECT_EQ(docgen::patch_action_to_string(docgen::PatchAction::Delete), "delete");
    
    auto a1 = docgen::patch_action_from_string("replace");
    EXPECT_TRUE(a1.has_value());
    EXPECT_EQ(*a1, docgen::PatchAction::Replace);
    
    auto a2 = docgen::patch_action_from_string("invalid");
    EXPECT_FALSE(a2.has_value());
}

TEST_F(DocgenTypesTest, TextPatch_Serialization) {
    docgen::TextPatch p;
    p.action = docgen::PatchAction::Replace;
    p.old_text = "old content";
    p.new_text = "new content";
    p.line_hint = 42;
    
    json j = p;
    EXPECT_EQ(j["action"], "replace");
    EXPECT_EQ(j["old_text"], "old content");
    EXPECT_EQ(j["new_text"], "new content");
    EXPECT_EQ(j["line_hint"], 42);
    
    auto p2 = j.get<docgen::TextPatch>();
    EXPECT_EQ(p2.action, p.action);
    EXPECT_EQ(p2.old_text, p.old_text);
    EXPECT_EQ(p2.new_text, p.new_text);
    EXPECT_TRUE(p2.line_hint.has_value());
    EXPECT_EQ(*p2.line_hint, 42);
}

TEST_F(DocgenTypesTest, TextPatch_DeleteAction) {
    docgen::TextPatch p;
    p.action = docgen::PatchAction::Delete;
    p.old_text = "text to delete";
    p.new_text = "";
    
    json j = p;
    auto p2 = j.get<docgen::TextPatch>();
    EXPECT_EQ(p2.action, docgen::PatchAction::Delete);
    EXPECT_TRUE(p2.new_text.empty());
}

TEST_F(DocgenTypesTest, PatchWriterResult_Serialization) {
    docgen::PatchWriterResult r;
    r.patches.push_back({docgen::PatchAction::Replace, "old", "new", std::nullopt});
    r.rationale = "Fix typo";
    
    json j = r;
    EXPECT_EQ(j["patches"].size(), 1);
    EXPECT_EQ(j["rationale"], "Fix typo");
    
    auto r2 = j.get<docgen::PatchWriterResult>();
    EXPECT_EQ(r2.patches.size(), 1);
    EXPECT_TRUE(r2.rationale.has_value());
    EXPECT_EQ(*r2.rationale, "Fix typo");
}

TEST_F(DocgenTypesTest, PatchWriterResult_EmptyPatches) {
    docgen::PatchWriterResult r;
    
    json j = r;
    EXPECT_TRUE(j["patches"].empty());
    EXPECT_FALSE(j.contains("rationale"));
    
    auto r2 = j.get<docgen::PatchWriterResult>();
    EXPECT_TRUE(r2.patches.empty());
    EXPECT_FALSE(r2.rationale.has_value());
}

TEST_F(DocgenTypesTest, MicroReviewResult_Approve) {
    docgen::MicroReviewResult r;
    r.verdict = docgen::ReviewVerdict::Approve;
    r.reason = "Change looks correct";
    
    json j = r;
    EXPECT_EQ(j["verdict"], "approve");
    EXPECT_EQ(j["reason"], "Change looks correct");
    
    auto r2 = j.get<docgen::MicroReviewResult>();
    EXPECT_EQ(r2.verdict, docgen::ReviewVerdict::Approve);
}

TEST_F(DocgenTypesTest, MicroReviewResult_Reject) {
    docgen::MicroReviewResult r;
    r.verdict = docgen::ReviewVerdict::Reject;
    r.reason = "Found issues";
    r.issues = {"Broken link", "Missing comma"};
    
    json j = r;
    EXPECT_EQ(j["verdict"], "reject");
    EXPECT_EQ(j["issues"].size(), 2);
    
    auto r2 = j.get<docgen::MicroReviewResult>();
    EXPECT_EQ(r2.verdict, docgen::ReviewVerdict::Reject);
    EXPECT_EQ(r2.issues.size(), 2);
}

TEST_F(DocgenTypesTest, DocOutline_Serialization) {
    docgen::DocOutline outline;
    outline.file_path = "README.md";
    outline.headings.push_back({1, 1, "Title"});
    outline.headings.push_back({2, 10, "Section 1"});
    outline.headings.push_back({3, 15, "Subsection"});
    
    json j = outline;
    EXPECT_EQ(j["file_path"], "README.md");
    EXPECT_EQ(j["headings"].size(), 3);
    EXPECT_EQ(j["headings"][0]["level"], 1);
    EXPECT_EQ(j["headings"][0]["line_number"], 1);
    EXPECT_EQ(j["headings"][0]["text"], "Title");
    
    auto o2 = j.get<docgen::DocOutline>();
    EXPECT_EQ(o2.file_path, outline.file_path);
    EXPECT_EQ(o2.headings.size(), 3);
}

TEST_F(DocgenTypesTest, SubagentContext_DefaultValues) {
    docgen::SubagentContext ctx;
    EXPECT_EQ(ctx.max_input_bytes, 16000);
    EXPECT_EQ(ctx.timeout_ms, 60000);
    EXPECT_EQ(ctx.max_retries, 2);
}

TEST_F(DocgenTypesTest, RoundTrip_AllTypes) {
    docgen::ChangeAnalystResult analyst;
    analyst.affected_files = {{"a.cpp", "modify", std::nullopt, {"f1"}}};
    analyst.intent_summary = "test";
    
    json j1 = analyst;
    auto a2 = j1.get<docgen::ChangeAnalystResult>();
    EXPECT_EQ(a2.intent_summary, "test");
    
    docgen::PatchWriterResult writer;
    writer.patches = {{docgen::PatchAction::Replace, "a", "b", std::nullopt}};
    writer.rationale = "r";
    
    json j2 = writer;
    auto w2 = j2.get<docgen::PatchWriterResult>();
    EXPECT_EQ(w2.patches[0].old_text, "a");
    
    docgen::MicroReviewResult review;
    review.verdict = docgen::ReviewVerdict::Approve;
    review.reason = "ok";
    
    json j3 = review;
    auto r2 = j3.get<docgen::MicroReviewResult>();
    EXPECT_EQ(r2.verdict, docgen::ReviewVerdict::Approve);
}

TEST_F(DocgenTypesTest, InvalidPatchAction_Throws) {
    json j = {{"action", "invalid_action"}, {"old_text", "a"}, {"new_text", "b"}};
    EXPECT_THROW(j.get<docgen::TextPatch>(), std::runtime_error);
}
