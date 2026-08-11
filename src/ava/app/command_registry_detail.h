#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/command_registry.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ava::app::runtime {
class Session;
struct ExtensionResourcePolicy;
}  // namespace ava::app::runtime

namespace ava::app {

// Internal builder accumulator shared by all command-registry loaders.
struct RegistryBuilder
{
  CommandRegistry registry;
  std::unordered_map<std::string, std::size_t> occupied;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Append `diagnostic` to the registry accumulated by `builder`.
void add_diagnostic(RegistryBuilder& builder, CommandRegistryDiagnostic diagnostic);

// Add `entry` and its aliases to `builder`, rejecting invalid tokens or collisions with a diagnostic.
//
// Returns true only when the entry was added.
bool add_entry(RegistryBuilder& builder, CommandRegistryEntry entry);

// Build a slash-command token from `prefix`, `id`, and optional `name` segments.
[[nodiscard]] std::string namespaced_command(std::string_view prefix, std::string_view id, std::string_view name = {});

// Parse shell-like command argument `text` into unescaped tokens while rejecting malformed quoting and control bytes.
[[nodiscard]] ava::core::Result<std::vector<std::string>> parse_command_argument_tokens(std::string_view text);

// Discover MCP prompts permitted by `options` and append their command entries or diagnostics to `builder` using the locked `session` and `policy`.
void load_mcp_prompt_commands(RegistryBuilder& builder, runtime::session_ts& unlocked_session, CommandRegistryOptions const& options,
                              runtime::ExtensionResourcePolicy const& policy);

}  // namespace ava::app
