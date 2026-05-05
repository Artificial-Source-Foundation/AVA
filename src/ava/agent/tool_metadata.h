#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace ava::agent {

struct ToolMetadata {
  std::string_view name;
  std::string_view description;
  std::string_view schema_json;
  std::string_view permission_category;
  std::string_view output_bound_summary;
  std::string_view execution_mode;
  std::string_view event_rendering_hint;
  // Reserved for future provider/model-specific description variants without changing the tool registry shape.
  std::optional<std::string_view> description_family;
};

inline constexpr std::array<ToolMetadata, 13>
    kBuiltinToolMetadata{
        {
            ToolMetadata{
                .name = "read_file",
                .description = "Read a text file through AVA permission checks. Use offset and limit to continue "
                               "large files by line.",
                .schema_json =
                    R"({"type":"function","name":"read_file","description":"Read a text file through AVA permission checks. Prefer this over shell commands for inspecting file contents. Use offset and limit to continue large files by line; use max_bytes only to bound very large ranges.","parameters":{"type":"object","properties":{"path":{"type":"string","description":"Workspace-relative path, or an absolute path that will require permission if outside the workspace."},"max_bytes":{"type":"integer","minimum":1,"maximum":524288,"description":"Maximum bytes returned for this call. Defaults to 51200 and is capped at 524288."},"offset":{"type":"integer","minimum":1,"description":"1-based line number to start reading from. Defaults to 1."},"limit":{"type":"integer","minimum":1,"maximum":100000,"description":"Maximum number of lines to return from offset. Omit to read until max_bytes or EOF."}},"required":["path"]}})",
                .permission_category = "read",
                .output_bound_summary =
                    "File content is bounded by max_bytes, optional line limit, and tool-level caps.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_read",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{
                .name = "list_directory",
                .description = "List readable entries in one directory through AVA permission checks.",
                .schema_json =
                    R"({"type":"function","name":"list_directory","description":"List readable files and subdirectories in one directory. Use this to orient before glob/grep or edits; it returns names, type, and size only.","parameters":{"type":"object","properties":{"path":{"type":"string","description":"Directory to list. Defaults to the workspace root."},"max_entries":{"type":"integer","minimum":1,"maximum":5000,"description":"Maximum entries returned. Defaults to 500 and is capped at 5000."}}}})",
                .permission_category = "search",
                .output_bound_summary =
                    "Directory entries are bounded by max_entries and omit paths denied by read policy.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_search",
                .description_family = std::string_view("search")},
            ToolMetadata{
                .name = "write_file",
                .description = "Write a full file through AVA permission checks. Use for new files or intentional "
                               "full rewrites.",
                .schema_json =
                    R"({"type":"function","name":"write_file","description":"Write a full file through AVA permission checks. Use for new files or intentional full rewrites; use edit_file or apply_patch for small changes to existing files. Denied for source files in plan mode.","parameters":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"]}})",
                .permission_category = "edit",
                .output_bound_summary = "Returns write status and byte count only.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_write",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{
                .name = "edit_file",
                .description = "Replace one exact unique text span in a file through AVA permission checks.",
                .schema_json =
                    R"({"type":"function","name":"edit_file","description":"Replace one exact unique text span in a file through AVA permission checks. Keep old_text small but uniquely identifiable, preserve exact whitespace and line endings, and use apply_patch for multiple replacements.","parameters":{"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string","minLength":1,"description":"Exact text to replace; it must occur exactly once."},"new_text":{"type":"string","description":"Replacement text."}},"required":["path","old_text","new_text"]}})",
                .permission_category = "edit",
                .output_bound_summary = "Returns edit status and byte count only.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_edit",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{.name = "glob",
                         .description = "Find readable workspace files by glob pattern.",
                         .schema_json =
                             R"({"type":"function","name":"glob","description":"Find readable workspace files by glob pattern. Use this for file discovery when you know filename shapes; use grep for content search and list_directory for one directory.","parameters":{"type":"object","properties":{"pattern":{"type":"string","description":"Glob pattern such as **/*.cpp or src/**/*.h. Bracket character classes are not supported."},"max_results":{"type":"integer","minimum":1,"maximum":10000,"description":"Maximum paths returned. Defaults to 2000 and is capped at 10000."}},"required":["pattern"]}})",
                         .permission_category = "search",
                         .output_bound_summary = "Matched paths are bounded by max_results and tool-level caps.",
                         .execution_mode = "synchronous",
                         .event_rendering_hint = "file_search",
                         .description_family = std::string_view("search")},
            ToolMetadata{
                .name = "grep",
                .description = "Search readable workspace files for text or a regular expression.",
                .schema_json =
                    R"({"type":"function","name":"grep","description":"Search readable workspace files for text. Defaults to literal, case-sensitive matching; set literal=false for ECMAScript regex and case_insensitive=true when casing is uncertain. Use read_file on matches for surrounding context.","parameters":{"type":"object","properties":{"pattern":{"type":"string","description":"Literal text by default, or an ECMAScript regex when literal is false."},"include":{"type":"string","description":"Glob limiting searched files. Defaults to **/*."},"max_matches":{"type":"integer","minimum":1,"maximum":10000,"description":"Maximum matched lines returned. Defaults to 2000 and is capped at 10000."},"literal":{"type":"boolean","description":"When true, pattern is matched literally. Defaults to true."},"case_insensitive":{"type":"boolean","description":"When true, matching ignores ASCII case. Defaults to false."}},"required":["pattern"]}})",
                .permission_category = "search",
                .output_bound_summary = "Matched lines are bounded by max_matches and line truncation caps.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_search",
                .description_family = std::string_view("search")},
            ToolMetadata{
                .name = "bash",
                .description = "Run a permissioned local argv-style command for builds, tests, and verification.",
                .schema_json =
                    R"({"type":"function","name":"bash","description":"Run a permissioned local argv-style command for builds, tests, and verification. AVA does not run a shell here: avoid pipes, redirects, variables, subshells, and other shell metacharacters. Prefer read_file, list_directory, glob, and grep for inspection.","parameters":{"type":"object","properties":{"command":{"type":"string","description":"Command plus arguments, separated by spaces, with no shell metacharacters."},"timeout_ms":{"type":"integer","minimum":1,"maximum":120000,"description":"Timeout in milliseconds. Defaults to 30000 and is capped at 120000."},"max_bytes":{"type":"integer","minimum":1,"maximum":524288,"description":"Maximum output bytes returned. Defaults to 51200 and is capped at 524288."}},"required":["command"]}})",
                .permission_category = "bash",
                .output_bound_summary = "Command output is bounded by timeout_ms, max_bytes, and tool-level caps.",
                .execution_mode = "synchronous_process",
                .event_rendering_hint = "command",
                .description_family = std::string_view("process")},
            ToolMetadata{.name = "webfetch",
                         .description =
                             "Fetch bounded content from a known http or https URL after network permission approval.",
                         .schema_json = R"({"type":"function","name":"webfetch","description":"Fetch bounded content from a known http or https URL after network permission approval. Use websearch first when you need to discover relevant URLs. Defaults to markdown output; use text or html when exact source format matters.","parameters":{"type":"object","properties":{"url":{"type":"string","description":"Fully formed http or https URL to fetch."},"format":{"type":"string","enum":["markdown","text","html"],"description":"Return format. Defaults to markdown."},"max_bytes":{"type":"integer","minimum":1,"maximum":5242880,"description":"Defaults to 1048576 and is capped at 5242880."},"timeout_ms":{"type":"integer","minimum":1000,"maximum":120000,"description":"Defaults to 30000 and is clamped to 1000-120000."}},"required":["url"]}})",
                         .permission_category = "network.fetch",
                         .output_bound_summary = "Response content is bounded by max_bytes and a fixed 5 MiB tool cap.",
                         .execution_mode = "synchronous_network",
                         .event_rendering_hint = "network_fetch",
                         .description_family = std::string_view("network")},
            ToolMetadata{
                .name = "websearch",
                .description = "Search the web for current sources after network permission approval.",
                .schema_json =
                    R"({"type":"function","name":"websearch","description":"Search the web for current sources after network permission approval. Use this for discovery when you do not already know the URL, then use webfetch on specific results for deeper reading.","parameters":{"type":"object","properties":{"query":{"type":"string","description":"Search query. Include the current year when looking for recent information."},"num_results":{"type":"integer","minimum":1,"maximum":10,"description":"Maximum results returned. Defaults to 8 and is capped at 10."},"context_max_chars":{"type":"integer","minimum":1,"maximum":30000,"description":"Maximum result text characters retained. Defaults to 10000 and is capped at 30000. The OpenCode-style alias contextMaxCharacters is also accepted."},"timeout_ms":{"type":"integer","minimum":1000,"maximum":60000,"description":"Defaults to 25000 and is clamped to 1000-60000."}},"required":["query"]}})",
                .permission_category = "network.search",
                .output_bound_summary = "Search results are bounded by num_results and context_max_chars.",
                .execution_mode = "synchronous_network",
                .event_rendering_hint = "network_search",
                .description_family = std::string_view("network")},
            ToolMetadata{
                .name = "skill",
                .description = "Load a listed local or global SKILL.md instruction file into the conversation.",
                .schema_json =
                    R"({"type":"function","name":"skill","description":"Load a listed local or global SKILL.md instruction file into the conversation. Use this when the task matches a skill shown in the available_skills system prompt section.","parameters":{"type":"object","properties":{"name":{"type":"string","description":"Skill name from available_skills."}},"required":["name"]}})",
                .permission_category = "skill",
                .output_bound_summary =
                    "Skill content is bounded by the skill loader file-size cap and includes a sampled file list.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "skill",
                .description_family = std::string_view("skills")},
            ToolMetadata{
                .name = "lsp_diagnostics",
                .description = "Query configured local language-server diagnostics for one workspace file.",
                .schema_json =
                    R"({"type":"function","name":"lsp_diagnostics","description":"Query configured local language-server diagnostics for one workspace file.","parameters":{"type":"object","properties":{"path":{"type":"string","maxLength":4096}},"required":["path"]}})",
                .permission_category = "lsp.query",
                .output_bound_summary =
                    "Returns structured diagnostics only; server command configuration is local-only.",
                .execution_mode = "synchronous_process",
                .event_rendering_hint = "lsp_diagnostics",
                .description_family = std::string_view("lsp")},
            ToolMetadata{
                .name = "apply_patch",
                .description = "Apply up to 32 exact text replacements across files through AVA permission checks.",
                .schema_json =
                    R"({"type":"function","name":"apply_patch","description":"Apply up to 32 exact text replacements across files through AVA permission checks. Use this for coordinated edits; each old_text must exist exactly once in its file and edits are validated before writes are committed.","parameters":{"type":"object","properties":{"edits":{"type":"array","minItems":1,"maxItems":32,"items":{"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string","minLength":1,"description":"Exact text to replace; it must occur exactly once in the file."},"new_text":{"type":"string"}},"required":["path","old_text","new_text"]}}},"required":["edits"]}})",
                .permission_category = "edit",
                .output_bound_summary = "Applies at most 32 edits and returns per-file byte counts.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_edit",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{
                .name = "question",
                .description = "Ask the user a clarification question through AVA's backend question resolver.",
                .schema_json =
                    R"({"type":"function","name":"question","description":"Ask the user a clarification question through AVA's backend question resolver.","parameters":{"type":"object","properties":{"header":{"type":"string"},"question":{"type":"string"},"options":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"object","properties":{"value":{"type":"string"},"label":{"type":"string"}}}]}},"multiple":{"type":"boolean"},"allow_multiple":{"type":"boolean"},"custom":{"type":"boolean"},"allow_custom":{"type":"boolean"}},"required":["question"]}})",
                .permission_category = "user",
                .output_bound_summary = "Returns the bounded user selection or custom answer text.",
                .execution_mode = "synchronous_user_interaction",
                .event_rendering_hint = "question",
                .description_family = std::string_view("interaction")},
        }};

[[nodiscard]] inline constexpr std::span<ToolMetadata const> builtin_tool_metadata() noexcept
{
  return std::span<ToolMetadata const>(kBuiltinToolMetadata.data(), kBuiltinToolMetadata.size());
}

}  // namespace ava::agent
