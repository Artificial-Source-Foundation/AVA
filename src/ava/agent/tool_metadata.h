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

inline constexpr std::array<ToolMetadata, 10>
    kBuiltinToolMetadata{
        {
            ToolMetadata{
                .name = "read_file",
                .description = "Read a workspace file through AVA permission checks.",
                .schema_json =
                    R"({"type":"function","name":"read_file","description":"Read a workspace file through AVA permission checks.","parameters":{"type":"object","properties":{"path":{"type":"string"},"max_bytes":{"type":"integer"}},"required":["path"]}})",
                .permission_category = "read",
                .output_bound_summary = "File content is bounded by max_bytes and tool-level caps.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_read",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{
                .name = "write_file",
                .description =
                    "Write a workspace file through AVA permission checks. Denied for source files in plan mode.",
                .schema_json =
                    R"({"type":"function","name":"write_file","description":"Write a workspace file through AVA permission checks. Denied for source files in plan mode.","parameters":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"]}})",
                .permission_category = "edit",
                .output_bound_summary = "Returns write status and byte count only.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_write",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{
                .name = "edit_file",
                .description = "Replace one unique text span in a workspace file through AVA permission checks.",
                .schema_json =
                    R"({"type":"function","name":"edit_file","description":"Replace one unique text span in a workspace file through AVA permission checks.","parameters":{"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string","minLength":1},"new_text":{"type":"string"}},"required":["path","old_text","new_text"]}})",
                .permission_category = "edit",
                .output_bound_summary = "Returns edit status and byte count only.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_edit",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{
                .name = "glob",
                .description = "List readable workspace files matching a glob pattern.",
                .schema_json =
                    R"({"type":"function","name":"glob","description":"List readable workspace files matching a glob pattern.","parameters":{"type":"object","properties":{"pattern":{"type":"string"},"max_results":{"type":"integer"}},"required":["pattern"]}})",
                .permission_category = "search",
                .output_bound_summary = "Matched paths are bounded by max_results and tool-level caps.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_search",
                .description_family = std::string_view("search")},
            ToolMetadata{
                .name = "grep",
                .description = "Search readable workspace files for literal text.",
                .schema_json =
                    R"({"type":"function","name":"grep","description":"Search readable workspace files for literal text.","parameters":{"type":"object","properties":{"pattern":{"type":"string"},"include":{"type":"string"},"max_matches":{"type":"integer"}},"required":["pattern"]}})",
                .permission_category = "search",
                .output_bound_summary = "Matched lines are bounded by max_matches and line truncation caps.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_search",
                .description_family = std::string_view("search")},
            ToolMetadata{
                .name = "bash",
                .description = "Run a permissioned local command without shell metacharacters.",
                .schema_json =
                    R"({"type":"function","name":"bash","description":"Run a permissioned local command without shell metacharacters.","parameters":{"type":"object","properties":{"command":{"type":"string"},"timeout_ms":{"type":"integer"},"max_bytes":{"type":"integer"}},"required":["command"]}})",
                .permission_category = "bash",
                .output_bound_summary = "Command output is bounded by timeout_ms, max_bytes, and tool-level caps.",
                .execution_mode = "synchronous_process",
                .event_rendering_hint = "command",
                .description_family = std::string_view("process")},
            ToolMetadata{
                .name = "webfetch",
                .description =
                    "Fetch bounded text content from an http or https URL after network permission approval.",
                .schema_json =
                    R"({"type":"function","name":"webfetch","description":"Fetch bounded text content from an http or https URL after network permission approval.","parameters":{"type":"object","properties":{"url":{"type":"string"},"max_bytes":{"type":"integer","minimum":1,"maximum":5242880,"description":"Defaults to 1048576 and is capped at 5242880."},"timeout_ms":{"type":"integer","minimum":1000,"maximum":120000,"description":"Defaults to 30000 and is clamped to 1000-120000."}},"required":["url"]}})",
                .permission_category = "network.fetch",
                .output_bound_summary = "Response content is bounded by max_bytes and a fixed 5 MiB tool cap.",
                .execution_mode = "synchronous_network",
                .event_rendering_hint = "network_fetch",
                .description_family = std::string_view("network")},
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
                .description = "Apply up to 32 exact text replacements across workspace files. Each old_text must "
                               "exist exactly once.",
                .schema_json =
                    R"({"type":"function","name":"apply_patch","description":"Apply up to 32 exact text replacements across workspace files. Each old_text must exist exactly once.","parameters":{"type":"object","properties":{"edits":{"type":"array","minItems":1,"maxItems":32,"items":{"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string","minLength":1},"new_text":{"type":"string"}},"required":["path","old_text","new_text"]}}},"required":["edits"]}})",
                .permission_category = "edit",
                .output_bound_summary = "Applies at most 32 edits and returns per-file byte counts.",
                .execution_mode = "synchronous",
                .event_rendering_hint = "file_edit",
                .description_family = std::string_view("filesystem")},
            ToolMetadata{.name = "question",
                         .description =
                             "Ask the user a clarification question through AVA's backend question resolver.",
                         .schema_json = R"({"type":"function","name":"question","description":"Ask the user a clarification question through AVA's backend question resolver.","parameters":{"type":"object","properties":{"header":{"type":"string"},"question":{"type":"string"},"options":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"object","properties":{"value":{"type":"string"},"label":{"type":"string"}}}]}},"multiple":{"type":"boolean"},"allow_multiple":{"type":"boolean"},"custom":{"type":"boolean"},"allow_custom":{"type":"boolean"}},"required":["question"]}})",
                         .permission_category = "user",
                         .output_bound_summary = "Returns the bounded user selection or custom answer text.",
                         .execution_mode = "synchronous_user_interaction",
                         .event_rendering_hint = "question",
                         .description_family = std::string_view("interaction")},
        }};

[[nodiscard]] inline constexpr std::span<ToolMetadata const> builtin_tool_metadata() noexcept {
  return std::span<ToolMetadata const>(kBuiltinToolMetadata.data(), kBuiltinToolMetadata.size());
}

}  // namespace ava::agent
