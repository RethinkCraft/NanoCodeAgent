#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

inline constexpr size_t kMaxGitContextLines = 1000;

nlohmann::json list_files_bounded(const std::string& workspace_abs,
                                  const std::string& directory,
                                  const std::vector<std::string>& extensions,
                                  size_t max_results,
                                  size_t output_limit = 0);

nlohmann::json rg_search(const std::string& workspace_abs,
                         const std::string& query,
                         const std::string& directory,
                         size_t max_results,
                         size_t max_snippet_bytes,
                         size_t output_limit = 0);

nlohmann::json git_status(const std::string& workspace_abs,
                          size_t max_entries,
                          size_t output_limit = 0);

nlohmann::json git_diff(const std::string& workspace_abs,
                        bool cached,
                        const std::vector<std::string>& pathspecs,
                        size_t context_lines,
                        size_t output_limit = 0);

nlohmann::json git_show(const std::string& workspace_abs,
                        const std::string& rev,
                        bool patch,
                        bool stat,
                        const std::vector<std::string>& pathspecs,
                        size_t context_lines,
                        size_t output_limit = 0);

void set_rg_binary_for_testing(const std::string& path);
void clear_rg_binary_for_testing();
