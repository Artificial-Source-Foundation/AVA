#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/agent/mode.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/permissions/permission.h"
#include "ava/core/error.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_text_file_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

void test_search_tools()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);

  auto const workspace = temp_root() / "workspace";
  ava::tools::ToolContext const context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};

  expect(ava::tools::write_file(context, workspace / "src" / "main.cpp", "int main() { return 0; }\n").has_value(), "search setup writes source");
  expect(ava::tools::write_file(context, workspace / "root.cpp", "int root() { return 0; }\n").has_value(), "search setup writes root source");
  expect(ava::tools::write_file(context, workspace / "docs" / "plan.md", "hello ava\nhello again\n").has_value(), "search setup writes docs");
  expect(ava::tools::write_file(context, workspace / "build" / "ignored.txt", "hello hidden\n").has_value(), "search setup writes ignored file");
  {
    std::ofstream secret_file(workspace / ".env", std::ios::binary | std::ios::trunc);
    secret_file << "hello secret\n";
  }
  std::filesystem::create_directories(workspace / ".ssh");
  {
    std::ofstream key_file(workspace / ".ssh" / "id_rsa", std::ios::binary | std::ios::trunc);
    key_file << "hello key\n";
  }

  auto glob = ava::tools::glob_files(context, "**/*.cpp");
  expect(glob.has_value(), "glob_files succeeds");
  if (glob)
  {
    expect(glob->paths.size() == 2, "glob_files returns nested and root source files");
    expect(glob->paths.size() < 2 || glob->paths[0].generic_string() < glob->paths[1].generic_string(),
           "glob_files sorts results deterministically by generic path string");
  }

  auto bracket_glob = ava::tools::glob_files(context, "*.[ch]");
  expect(!bracket_glob && bracket_glob.error().category() == ava::core::ErrorCategory::InvalidArgument &&
              bracket_glob.error().message().find("bracket") != std::string::npos,
          "glob_files rejects unsupported bracket character classes instead of silently mis-matching them");

  auto oversized_glob = ava::tools::glob_files(context, std::string(513, '*'));
  expect(!oversized_glob && oversized_glob.error().category() == ava::core::ErrorCategory::InvalidArgument &&
             oversized_glob.error().message().find("maximum length") != std::string::npos,
         "glob_files rejects oversized patterns before traversal");

  auto result_capped = ava::tools::glob_files(context, "**/*.cpp", ava::tools::GlobOptions{.max_results = 1});
  expect(result_capped && result_capped->paths.size() == 1 && result_capped->total_matches == 2 && result_capped->truncated,
         "glob_files reports result-count truncation while counting all matches");

  auto const spill_dir = temp_root() / "session" / "spill";
  ava::tools::ToolContext const spilling_context{
      .workspace_dir = workspace, .spill_dir = spill_dir, .mode = ava::agent::Mode::Build, .current_tool_name = "glob", .current_call_id = "call/glob"};
  auto spilling_glob = ava::tools::glob_files(spilling_context, "**/*.cpp", ava::tools::GlobOptions{.max_results = 1});
  expect(spilling_glob && spilling_glob->truncated && !spilling_glob->spill_path.empty() && spilling_glob->spill_path.parent_path() == spill_dir &&
             std::filesystem::exists(spilling_glob->spill_path),
         "glob_files writes truncated results to the configured spill directory");
  if (spilling_glob && !spilling_glob->spill_path.empty())
  {
    auto const spill_text = read_text_file_for_test(spilling_glob->spill_path);
    expect(spill_text.find("main.cpp") != std::string::npos && spill_text.find("root.cpp") != std::string::npos,
           "glob spill file records one path per line for all matched paths before the result cap");
    expect(spilling_glob->spill_path.filename().string().find('/') == std::string::npos, "glob spill filename contains no user-controlled path separators");
  }

  auto capped = ava::tools::glob_files(context, "**/*", ava::tools::GlobOptions{.max_results = 2000, .max_visited = 1});
  expect(capped && capped->truncated, "glob_files reports traversal cap truncation");

  ava::tools::ToolContext const canceled_search_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .cancel_requested = [] { return true; },
  };
  auto canceled_glob = ava::tools::glob_files(canceled_search_context, "**/*");
  expect(!canceled_glob && canceled_glob.error().message() == "tool canceled", "glob_files observes cancellation before traversal");
  auto canceled_grep = ava::tools::grep_files(canceled_search_context, "hello", "**/*");
  expect(!canceled_grep && canceled_grep.error().message() == "tool canceled", "grep_files observes cancellation before search");

  auto grep = ava::tools::grep_files(context, "hello", "**/*.md");
  expect(grep.has_value(), "grep_files succeeds");
  if (grep)
  {
    expect(grep->matches.size() == 2, "grep_files returns matching markdown lines");
    expect(grep->matches[0].line_number == 1, "grep_files records line numbers");
  }
  auto ci_options = ava::tools::GrepOptions{};
  ci_options.case_insensitive = true;
  auto case_insensitive = ava::tools::grep_files(context, "HELLO", "**/*.md", ci_options);
  expect(case_insensitive && case_insensitive->matches.size() == 2, "grep_files supports explicit case-insensitive matching");
  auto regex_options = ava::tools::GrepOptions{};
  regex_options.literal = false;
  auto regex = ava::tools::grep_files(context, "hello (ava|again)", "**/*.md", regex_options);
  expect(regex && regex->matches.size() == 2, "grep_files supports provider-selected regex matching");
  auto bad_regex = ava::tools::grep_files(context, "(", "**/*.md", regex_options);
  expect(!bad_regex && bad_regex.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "grep_files reports invalid regex patterns as argument errors");

  auto listed = ava::tools::list_directory(context, workspace, ava::tools::ListDirectoryOptions{.max_entries = 20});
  auto const has_list_entry = [](ava::tools::ListDirectoryResult const& result, std::string const& name, bool directory) {
    return std::ranges::any_of(result.entries,
                               [&name, directory](ava::tools::DirectoryEntry const& entry) { return entry.name == name && entry.directory == directory; });
  };
  expect(listed && has_list_entry(*listed, "docs", true) && has_list_entry(*listed, "root.cpp", false) &&
             std::ranges::none_of(listed->entries, [](ava::tools::DirectoryEntry const& entry) { return entry.name == ".env" || entry.name == ".ssh"; }),
         "list_directory returns readable entries and omits paths denied by read policy");
  auto listed_capped = ava::tools::list_directory(context, workspace, ava::tools::ListDirectoryOptions{.max_entries = 1});
  expect(listed_capped && listed_capped->entries.size() == 1 && listed_capped->truncated && listed_capped->total_entries > listed_capped->entries.size(),
         "list_directory reports entry-count truncation");

  ava::tools::ToolContext const spilling_grep_context{
      .workspace_dir = workspace, .spill_dir = spill_dir, .mode = ava::agent::Mode::Build, .current_tool_name = "grep", .current_call_id = "call:grep"};
  auto spilling_grep = ava::tools::grep_files(spilling_grep_context, "hello", "**/*.md", ava::tools::GrepOptions{.max_matches = 1});
  expect(spilling_grep && spilling_grep->truncated && spilling_grep->matches.size() == 1 && !spilling_grep->spill_path.empty() &&
             spilling_grep->spill_path.parent_path() == spill_dir,
         "grep_files writes truncated matches to the configured spill directory");
  if (spilling_grep && !spilling_grep->spill_path.empty())
  {
    auto const spill_text = read_text_file_for_test(spilling_grep->spill_path);
    expect(spill_text.find("plan.md:1:hello ava") != std::string::npos && spill_text.find("plan.md:2:hello again") != std::string::npos,
           "grep spill file records path, line, and content for all matched lines before the result cap");
  }

  auto punctuation = ava::tools::grep_files(context, "main()", "**/*.cpp");
  expect(punctuation && !punctuation->matches.empty(), "grep_files literal search accepts punctuation");

  auto truncated = ava::tools::grep_files(context, "int", "**/*.cpp", ava::tools::GrepOptions{.max_line_length = 5});
  expect(truncated && !truncated->matches.empty() && truncated->matches[0].line_truncated, "grep_files reports line truncation metadata");

  auto ignored = ava::tools::grep_files(context, "hidden", "**/*");
  expect(ignored && ignored->matches.empty(), "grep_files skips generated folders");
  auto no_ignore_generated = ava::tools::grep_files(context, "hidden", "**/*", ava::tools::GrepOptions{.no_ignore = true});
  expect(no_ignore_generated &&
             std::ranges::any_of(no_ignore_generated->matches,
                                 [&workspace](ava::tools::GrepMatch const& match) { return match.path == workspace / "build" / "ignored.txt"; }),
         "grep_files no_ignore includes hardcoded generated-directory fallback matches");

  auto glob_secrets = ava::tools::glob_files(context, "**/*");
  expect(glob_secrets && std::ranges::none_of(glob_secrets->paths, [](std::filesystem::path const& path) { return path.filename() == "id_rsa"; }),
         "glob_files skips files denied by read policy");
  auto no_ignore_glob_secrets = ava::tools::glob_files(context, "**/*", ava::tools::GlobOptions{.no_ignore = true});
  expect(no_ignore_glob_secrets &&
             std::ranges::none_of(no_ignore_glob_secrets->paths,
                                  [](std::filesystem::path const& path) { return path.filename() == ".env" || path.filename() == "id_rsa"; }),
         "glob_files no_ignore still skips files denied by read policy");

  auto secret = ava::tools::grep_files(context, "secret", "**/*");
  expect(secret && secret->matches.empty(), "grep_files skips files denied by read policy");
  auto no_ignore_secret = ava::tools::grep_files(context, "secret", "**/*", ava::tools::GrepOptions{.no_ignore = true});
  expect(no_ignore_secret && no_ignore_secret->matches.empty(), "grep_files no_ignore still skips files denied by read policy");

  auto const outside_search_path = temp_root() / "outside-search.txt";
  {
    std::ofstream outside_file(outside_search_path, std::ios::binary | std::ios::trunc);
    outside_file << "outside hello\n";
  }
  std::error_code symlink_error;
  auto const outside_search_link = workspace / "outside-link.txt";
  std::filesystem::create_symlink(outside_search_path, outside_search_link, symlink_error);
  if (!symlink_error)
  {
    int search_prompts = 0;
    std::vector<ava::tools::PermissionAuditEvent> search_audits;
    ava::tools::ToolContext const resolving_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver = [&search_prompts, &outside_search_link](
                                   ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++search_prompts;
          expect(prompt.operation == ava::permissions::Operation::ReadFile, "search resolver receives read operation for symlink escapes");
          expect(prompt.target_path == outside_search_link, "search resolver receives the matching symlink path");
          return ava::permissions::PermissionResolution::Allow;
        },
        .permission_audit_sink = [&search_audits](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
          search_audits.push_back(event);
          return {};
        }};
    auto resolved_glob = ava::tools::glob_files(resolving_search_context, "**/*");
    bool const resolved_includes_link =
        resolved_glob && std::ranges::any_of(resolved_glob->paths, [&outside_search_link](auto const& path) { return path == outside_search_link; });
    expect(resolved_includes_link && search_prompts == 1, "glob_files resolves ask decisions for symlinked matches instead of silently skipping them");
    expect(search_audits.size() == 3 && search_audits[0].operation == ava::permissions::Operation::SearchFiles &&
               search_audits[0].action == ava::permissions::PermissionAction::Allow && search_audits[1].operation == ava::permissions::Operation::ReadFile &&
               search_audits[1].action == ava::permissions::PermissionAction::Ask && search_audits[2].resolution == "allow" &&
               search_audits[2].resolution_source == "resolver",
           "glob_files audits the search root and ask resolver outcome without per-file allow audits");

    int denied_search_prompts = 0;
    ava::tools::ToolContext const denying_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver =
            [&denied_search_prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++denied_search_prompts;
          return ava::permissions::PermissionResolution::Deny;
        }};
    auto denied_glob = ava::tools::glob_files(denying_search_context, "**/*");
    bool const denied_excludes_link =
        denied_glob && std::ranges::none_of(denied_glob->paths, [&outside_search_link](auto const& path) { return path == outside_search_link; });
    expect(denied_excludes_link && denied_search_prompts == 1, "glob_files keeps resolver-denied ask matches excluded");

    int failing_search_prompts = 0;
    ava::tools::ToolContext const failing_search_context{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .permission_resolver =
            [&failing_search_prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++failing_search_prompts;
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "search resolver failed"));
        }};
    auto failing_glob = ava::tools::glob_files(failing_search_context, "**/*");
    bool const failing_skips_link =
        failing_glob && std::ranges::none_of(failing_glob->paths, [&outside_search_link](auto const& path) { return path == outside_search_link; });
    bool const failing_keeps_readable_match =
        failing_glob && std::ranges::any_of(failing_glob->paths, [&workspace](auto const& path) { return path == workspace / "docs" / "plan.md"; });
    expect(failing_skips_link && failing_keeps_readable_match && failing_search_prompts == 1,
           "glob_files skips ask matches with resolver errors while continuing search");
  }

  {
    std::ofstream binary_file(workspace / "binary.bin", std::ios::binary | std::ios::trunc);
    binary_file << std::string("binary", 6) << '\0' << " marker\nhello from binary\n";
  }
  auto binary = ava::tools::grep_files(context, "hello", "**/*.bin");
  expect(binary && binary->matches.empty() && binary->total_matches == 0, "grep_files skips an entire file after detecting binary content");
  auto no_ignore_binary = ava::tools::grep_files(context, "hello", "**/*.bin", ava::tools::GrepOptions{.no_ignore = true});
  expect(no_ignore_binary && no_ignore_binary->matches.empty() && no_ignore_binary->total_matches == 0, "grep_files no_ignore still skips binary content");

  {
    std::ofstream binary_file(workspace / "overlong-binary.bin", std::ios::binary | std::ios::trunc);
    binary_file << std::string(32, 'x') << '\0' << " hello after nul\n";
  }
  auto overlong_binary = ava::tools::grep_files(context, "hello", "**/overlong-binary.bin", ava::tools::GrepOptions{.max_line_length = 5});
  expect(overlong_binary && overlong_binary->matches.empty() && overlong_binary->total_matches == 0,
         "grep_files treats NUL after an overlong truncation point as binary content");

  {
    std::ofstream binary_file(workspace / "early-match-binary.bin", std::ios::binary | std::ios::trunc);
    binary_file << "hello before binary marker\n" << '\0' << "binary tail\n";
  }
  ava::tools::ToolContext const binary_spill_context{
      .workspace_dir = workspace, .spill_dir = spill_dir, .mode = ava::agent::Mode::Build, .current_tool_name = "grep", .current_call_id = "call-binary-spill"};
  auto binary_spill_grep = ava::tools::grep_files(binary_spill_context, "hello", "**/*", ava::tools::GrepOptions{.max_matches = 1});
  expect(binary_spill_grep && binary_spill_grep->truncated && !binary_spill_grep->spill_path.empty(),
         "grep_files writes a spill file for truncated non-binary matches");
  if (binary_spill_grep && !binary_spill_grep->spill_path.empty())
  {
    auto const spill_text = read_text_file_for_test(binary_spill_grep->spill_path);
    expect(spill_text.find("hello before binary marker") == std::string::npos, "grep spill files exclude matches from files later classified as binary");
  }
}

void test_search_gitignore_rules()
{
  auto const root = temp_root() / "search-ignore";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  ava::tools::ToolContext const context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};

  expect(ava::tools::write_file(context, workspace / ".gitignore",
                                "*.log\n"
                                "!keep.log\n"
                                "cache/\n"
                                "aaa_ignored/\n"
                                "logs/**/*.tmp\n"
                                "private/   \n"
                                "customer\\ data/\n"
                                "\\#literal\n"
                                "\\!literal\n"
                                "wildcard_private/*\n"
                                "!wildcard_private/\\*.txt\n")
             .has_value(),
         "ignore setup writes root .gitignore");
  expect(ava::tools::write_file(context, workspace / "app.log", "hidden root log\n").has_value(), "ignore setup writes root ignored file");
  expect(ava::tools::write_file(context, workspace / "keep.log", "visible negated log\n").has_value(), "ignore setup writes negated file");
  expect(ava::tools::write_file(context, workspace / "cache" / "data.txt", "hidden cache\n").has_value(), "ignore setup writes directory-only ignored file");
  expect(ava::tools::write_file(context, workspace / "logs" / "deep" / "trace.tmp", "hidden double star\n").has_value(),
         "ignore setup writes double-star ignored file");
  expect(ava::tools::write_file(context, workspace / "private" / "data.txt", "hidden trailing spaces\n").has_value(),
         "ignore setup writes file ignored by trailing-space rule");
  expect(ava::tools::write_file(context, workspace / "customer data" / "data.txt", "hidden escaped space\n").has_value(),
         "ignore setup writes file ignored by escaped-space rule");
  expect(ava::tools::write_file(context, workspace / "#literal", "hidden escaped comment\n").has_value(),
         "ignore setup writes file ignored by escaped comment rule");
  expect(ava::tools::write_file(context, workspace / "!literal", "hidden escaped negation\n").has_value(),
         "ignore setup writes file ignored by escaped negation rule");
  expect(ava::tools::write_file(context, workspace / "wildcard_private" / "customer.txt", "hidden wildcard\n").has_value(),
         "ignore setup writes file hidden by wildcard directory rule");
  expect(ava::tools::write_file(context, workspace / "wildcard_private" / "*.txt", "visible literal star\n").has_value(),
         "ignore setup writes file re-included by escaped wildcard negation");
  expect(ava::tools::write_file(context, workspace / "src" / ".gitignore", "/local.txt\n").has_value(), "ignore setup writes nested .gitignore");
  expect(ava::tools::write_file(context, workspace / "src" / "local.txt", "hidden nested local\n").has_value(), "ignore setup writes nested ignored file");
  expect(ava::tools::write_file(context, workspace / "src" / "nested" / "local.txt", "visible nested child\n").has_value(),
         "ignore setup writes file outside nested anchored rule");
  for (int index = 0; index < 50; ++index)
  {
    expect(ava::tools::write_file(context, workspace / "aaa_ignored" / ("ignored" + std::to_string(index) + ".txt"), "ignored traversal budget\n").has_value(),
           "ignore setup writes ignored directory entry");
  }
  expect(ava::tools::write_file(context, workspace / "zzz_late.txt", "visible late file\n").has_value(), "ignore setup writes a late visible file");

  auto const has_path = [](ava::tools::GlobResult const& result, std::filesystem::path const& path) {
    return std::ranges::any_of(result.paths, [&path](auto const& candidate) { return candidate == path; });
  };

  auto default_glob = ava::tools::glob_files(context, "**/*");
  expect(default_glob.has_value(), "glob_files succeeds with gitignore matcher");
  if (default_glob)
  {
    expect(!has_path(*default_glob, workspace / "app.log"), "root .gitignore ignores wildcard matches");
    expect(has_path(*default_glob, workspace / "keep.log"), "root .gitignore negation re-includes later matches");
    expect(!has_path(*default_glob, workspace / "cache" / "data.txt"), "directory-only .gitignore rules ignore descendants");
    expect(!has_path(*default_glob, workspace / "aaa_ignored" / "ignored0.txt"), "directory-only .gitignore rules prune ignored directory descendants");
    expect(!has_path(*default_glob, workspace / "logs" / "deep" / "trace.tmp"), "double-star .gitignore rules ignore deep descendants");
    expect(!has_path(*default_glob, workspace / "private" / "data.txt"),
           "unescaped trailing spaces do not prevent directory-only .gitignore rules from matching");
    expect(!has_path(*default_glob, workspace / "customer data" / "data.txt"), "backslash-escaped spaces in .gitignore rules match literal spaces");
    expect(!has_path(*default_glob, workspace / "#literal"), "backslash-escaped comment markers in .gitignore rules match literal filenames");
    expect(!has_path(*default_glob, workspace / "!literal"), "backslash-escaped negation markers in .gitignore rules match literal filenames");
    expect(!has_path(*default_glob, workspace / "wildcard_private" / "customer.txt"),
           "escaped wildcard negation does not re-include ordinary wildcard matches");
    expect(has_path(*default_glob, workspace / "wildcard_private" / "*.txt"), "escaped wildcard negation re-includes only the literal wildcard filename");
    expect(!has_path(*default_glob, workspace / "src" / "local.txt"), "nested .gitignore anchored rules are relative to the nested directory");
    expect(has_path(*default_glob, workspace / "src" / "nested" / "local.txt"), "nested anchored .gitignore rules do not ignore deeper same-name files");
  }

  auto no_ignore_glob = ava::tools::glob_files(context, "**/*", ava::tools::GlobOptions{.no_ignore = true});
  expect(no_ignore_glob && has_path(*no_ignore_glob, workspace / "app.log") && has_path(*no_ignore_glob, workspace / "cache" / "data.txt") &&
             has_path(*no_ignore_glob, workspace / "aaa_ignored" / "ignored0.txt") && has_path(*no_ignore_glob, workspace / "src" / "local.txt"),
         "glob_files no_ignore opt-out returns files ignored by .gitignore");

  auto pruned_glob = ava::tools::glob_files(context, "zzz_late.txt", ava::tools::GlobOptions{.max_visited = 30});
  expect(pruned_glob && !pruned_glob->truncated && has_path(*pruned_glob, workspace / "zzz_late.txt"),
         "glob_files prunes ignored directories before they exhaust the traversal budget");

  auto const external_ignore = root / "external-ignore";
  {
    std::ofstream file(external_ignore, std::ios::binary | std::ios::trunc);
    file << "*.txt\n";
  }
  std::error_code symlink_error;
  std::filesystem::create_directories(workspace / "symlinked", symlink_error);
  symlink_error.clear();
  std::filesystem::create_symlink(external_ignore, workspace / "symlinked" / ".gitignore", symlink_error);
  expect(ava::tools::write_file(context, workspace / "symlinked" / "visible.txt", "visible symlink ignore\n").has_value(),
         "ignore setup writes file next to symlinked .gitignore");
  if (!symlink_error)
  {
    auto symlink_glob = ava::tools::glob_files(context, "symlinked/visible.txt");
    expect(symlink_glob && has_path(*symlink_glob, workspace / "symlinked" / "visible.txt"),
           "glob_files does not follow symlinked .gitignore files outside the workspace");
  }

  auto default_grep = ava::tools::grep_files(context, "hidden", "**/*");
  expect(default_grep && std::ranges::none_of(default_grep->matches,
                                              [&workspace](ava::tools::GrepMatch const& match) {
                                                return match.path == workspace / "app.log" || match.path == workspace / "cache" / "data.txt" ||
                                                       match.path == workspace / "logs" / "deep" / "trace.tmp" || match.path == workspace / "src" / "local.txt";
                                              }),
         "grep_files respects .gitignore by default");

  auto no_ignore_grep = ava::tools::grep_files(context, "hidden", "**/*", ava::tools::GrepOptions{.no_ignore = true});
  expect(no_ignore_grep &&
             std::ranges::any_of(no_ignore_grep->matches, [&workspace](ava::tools::GrepMatch const& match) { return match.path == workspace / "app.log"; }) &&
             std::ranges::any_of(no_ignore_grep->matches,
                                 [&workspace](ava::tools::GrepMatch const& match) { return match.path == workspace / "src" / "local.txt"; }),
         "grep_files no_ignore opt-out searches files ignored by .gitignore");
}

}  // namespace

void run_tools_search_tests()
{
  test_search_tools();
  test_search_gitignore_rules();
}
