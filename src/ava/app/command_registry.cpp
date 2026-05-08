#include "ava/app/command_registry.h"

#include "ava/app/command_catalog.h"
#include "ava/app/command_format.h"
#include "ava/app/command_tools.h"

#include "ava/tools/file_tools.h"

#include "ava/plugin/diagnostics.h"

#include "ava/mcp/config.h"

#include "ava/context/skill_loader.h"

#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ava::app {
namespace {

constexpr std::size_t kMaxCommandFileBytes = 64 * 1024;
constexpr std::size_t kMaxCommandTokenBytes = 256;

struct RegistryBuilder {
  CommandRegistry registry;
  std::unordered_map<std::string, std::size_t> occupied;
};

struct ParsedMarkdown {
  std::map<std::string, std::string> frontmatter;
  std::string body;
};

std::string_view trim_view(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

std::string trim(std::string_view value)
{
  auto const trimmed = trim_view(value);
  return std::string(trimmed);
}

std::string strip_matching_quotes(std::string value)
{
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

std::string_view command_token(std::string_view line) noexcept
{
  auto const end = line.find_first_of(" \t\r\n");
  return line.substr(0, end == std::string_view::npos ? line.size() : end);
}

bool valid_command_segment(std::string_view segment)
{
  if (segment.empty() || segment.size() > 128) return false;
  bool last_was_separator = false;
  for (char const ch : segment) {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '.' || ch == '_' || ch == '-';
    if (!allowed) return false;
    bool const separator = ch == '.' || ch == '_' || ch == '-';
    if (separator && last_was_separator) return false;
    last_was_separator = separator;
  }
  return !last_was_separator;
}

bool valid_prompt_command_name(std::string_view name)
{
  if (name.empty() || name.size() > kMaxCommandTokenBytes - 1) return false;
  std::size_t start = 0;
  while (start <= name.size()) {
    auto const slash = name.find('/', start);
    auto const end = slash == std::string_view::npos ? name.size() : slash;
    if (!valid_command_segment(name.substr(start, end - start))) return false;
    if (slash == std::string_view::npos) break;
    start = slash + 1;
  }
  return true;
}

bool valid_command_token(std::string_view command)
{
  if (!command.starts_with('/') || command.size() < 2 || command.size() > kMaxCommandTokenBytes) return false;
  for (char const ch : command) {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '/' || ch == '.' || ch == '_' || ch == '-' || ch == ':';
    if (!allowed || byte < 0x20 || byte == 0x7F) return false;
  }
  return true;
}

void add_diagnostic(RegistryBuilder& builder, CommandRegistryDiagnostic diagnostic)
{
  builder.registry.diagnostics.push_back(std::move(diagnostic));
}

bool add_entry(RegistryBuilder& builder, CommandRegistryEntry entry)
{
  std::vector<std::string> tokens;
  tokens.push_back(entry.command);
  for (auto const& alias : entry.aliases) tokens.push_back(alias);
  for (auto const& token : tokens) {
    if (!valid_command_token(token)) {
      add_diagnostic(builder, CommandRegistryDiagnostic{.command = token,
                                                        .source = to_string(entry.source),
                                                        .source_id = entry.source_id,
                                                        .path = entry.source_path,
                                                        .message = "command token is invalid"});
      return false;
    }
    auto const existing = builder.occupied.find(token);
    if (existing == builder.occupied.end()) continue;
    auto const& winner = builder.registry.entries[existing->second];
    add_diagnostic(builder, CommandRegistryDiagnostic{.command = token,
                                                      .source = to_string(entry.source),
                                                      .source_id = entry.source_id,
                                                      .path = entry.source_path,
                                                      .message = "command collision; existing command kept",
                                                      .winner_source = to_string(winner.source),
                                                      .winner_source_id = winner.source_id,
                                                      .winner_path = winner.source_path});
    return false;
  }

  auto const index = builder.registry.entries.size();
  builder.registry.entries.push_back(std::move(entry));
  for (auto& token : tokens) builder.occupied.emplace(std::move(token), index);
  return true;
}

std::string markdown_field(ParsedMarkdown const& markdown, std::string_view name)
{
  auto const it = markdown.frontmatter.find(std::string(name));
  if (it == markdown.frontmatter.end()) return {};
  return it->second;
}

ParsedMarkdown parse_markdown(std::string_view content)
{
  ParsedMarkdown parsed;
  if (!(content.starts_with("---\n") || content.starts_with("---\r\n"))) {
    parsed.body = std::string(content);
    return parsed;
  }

  auto const body_start = content.starts_with("---\r\n") ? 5 : 4;
  auto const delimiter = content.find("\n---", body_start);
  if (delimiter == std::string_view::npos) {
    parsed.body = std::string(content);
    return parsed;
  }

  auto const frontmatter = content.substr(body_start, delimiter - body_start);
  std::size_t line_start = 0;
  while (line_start <= frontmatter.size()) {
    auto const line_end = frontmatter.find('\n', line_start);
    auto line = frontmatter.substr(line_start,
                                   line_end == std::string_view::npos ? std::string_view::npos : line_end - line_start);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (auto const colon = line.find(':'); colon != std::string_view::npos) {
      auto key = trim(line.substr(0, colon));
      auto value = strip_matching_quotes(trim(line.substr(colon + 1)));
      if (!key.empty()) parsed.frontmatter[std::move(key)] = std::move(value);
    }
    if (line_end == std::string_view::npos) break;
    line_start = line_end + 1;
  }

  auto after = delimiter + 4;
  if (after < content.size() && content[after] == '\r') ++after;
  if (after < content.size() && content[after] == '\n') ++after;
  parsed.body = std::string(content.substr(after));
  return parsed;
}

ava::core::Result<std::string> read_bounded_file(std::filesystem::path const& path, std::size_t max_bytes)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "command file is not a regular file");
    error.with_context("path", path.string());
    if (status_error) error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_bytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "command file is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_bytes));
    if (size_error) error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open command file")
                               .with_context("path", path.string()));
  }

  std::string content;
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0) content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_bytes) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "command file is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading command file")
                               .with_context("path", path.string()));
  }
  return content;
}

std::vector<std::filesystem::path> markdown_files(std::filesystem::path const& root,
                                                  std::vector<CommandRegistryDiagnostic>& diagnostics,
                                                  UnifiedCommandSource source)
{
  std::vector<std::filesystem::path> files;
  if (root.empty()) return files;
  std::error_code exists_error;
  if (!std::filesystem::exists(root, exists_error)) return files;
  if (exists_error) {
    diagnostics.push_back(CommandRegistryDiagnostic{
        .source = to_string(source), .path = root, .message = "failed to inspect command directory"});
    return files;
  }
  std::error_code directory_error;
  if (!std::filesystem::is_directory(root, directory_error) || directory_error) {
    diagnostics.push_back(CommandRegistryDiagnostic{
        .source = to_string(source), .path = root, .message = "command path is not a directory"});
    return files;
  }

  std::error_code iter_error;
  for (std::filesystem::recursive_directory_iterator
           it(root, std::filesystem::directory_options::skip_permission_denied, iter_error),
       end;
       !iter_error && it != end; it.increment(iter_error)) {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error) continue;
    if (!it->is_regular_file(entry_error) || entry_error) continue;
    if (it->path().extension() != ".md") continue;
    files.push_back(it->path());
  }
  if (iter_error) {
    diagnostics.push_back(CommandRegistryDiagnostic{
        .source = to_string(source), .path = root, .message = "failed to iterate command directory"});
  }
  std::ranges::sort(files);
  return files;
}

std::optional<std::string> command_name_for_file(std::filesystem::path const& root, std::filesystem::path const& file)
{
  std::error_code relative_error;
  auto relative = std::filesystem::relative(file, root, relative_error);
  if (relative_error || relative.empty()) return std::nullopt;
  relative.replace_extension();
  auto name = relative.generic_string();
  if (!valid_prompt_command_name(name)) return std::nullopt;
  return name;
}

void load_prompt_command_dir(RegistryBuilder& builder, std::filesystem::path const& root, UnifiedCommandSource source,
                             std::string source_scope)
{
  auto files = markdown_files(root, builder.registry.diagnostics, source);
  for (auto const& file : files) {
    auto name = command_name_for_file(root, file);
    if (!name) {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.source = to_string(source),
                                               .path = file,
                                               .message = "command file name does not form a safe slash command"});
      continue;
    }
    auto content = read_bounded_file(file, kMaxCommandFileBytes);
    if (!content) {
      add_diagnostic(
          builder,
          CommandRegistryDiagnostic{
              .command = "/" + *name, .source = to_string(source), .path = file, .message = content.error().format()});
      continue;
    }
    auto parsed = parse_markdown(*content);
    auto description = markdown_field(parsed, "description");
    auto hint = markdown_field(parsed, "argument-hint");
    if (hint.empty()) hint = markdown_field(parsed, "argument_hint");
    if (hint.empty()) hint = markdown_field(parsed, "hint");
    auto body = markdown_field(parsed, "template");
    if (body.empty()) body = std::move(parsed.body);
    if (trim_view(body).empty()) {
      add_diagnostic(builder, CommandRegistryDiagnostic{.command = "/" + *name,
                                                        .source = to_string(source),
                                                        .path = file,
                                                        .message = "command template is empty"});
      continue;
    }
    add_entry(builder,
              CommandRegistryEntry{.command = "/" + *name,
                                   .description = description.empty() ? "Prompt command " + *name : description,
                                   .hint = std::move(hint),
                                   .category = "Prompts",
                                   .source = source,
                                   .kind = UnifiedCommandKind::PromptTemplate,
                                   .source_id = std::move(*name),
                                   .source_path = file,
                                   .source_scope = std::move(source_scope),
                                   .template_text = std::move(body)});
  }
}

std::string namespaced_command(std::string_view prefix, std::string_view id, std::string_view name = {})
{
  std::string command = "/";
  command += prefix;
  command += ':';
  command += id;
  if (!name.empty()) {
    command += ':';
    command += name;
  }
  return command;
}

void load_builtin_commands(RegistryBuilder& builder)
{
  for (auto const& entry : command_catalog()) {
    add_entry(builder, CommandRegistryEntry{.command = entry.command,
                                            .aliases = entry.aliases,
                                            .description = entry.description,
                                            .hint = entry.hint,
                                            .category = entry.category,
                                            .enabled = entry.enabled,
                                            .disabled_reason = entry.disabled_reason,
                                            .source = UnifiedCommandSource::Builtin,
                                            .kind = UnifiedCommandKind::Backend,
                                            .source_id = "builtin"});
  }
}

void load_prompt_commands(RegistryBuilder& builder, RuntimeSession const& session)
{
  load_prompt_command_dir(builder, session.workspace_dir / ".ava" / "commands", UnifiedCommandSource::PromptProject,
                          "project");
  load_prompt_command_dir(builder, session.workspace_dir / ".ava" / "command", UnifiedCommandSource::PromptProject,
                          "project");
  load_prompt_command_dir(builder, session.paths.ava_config_dir / "commands", UnifiedCommandSource::PromptGlobal,
                          "global");
  load_prompt_command_dir(builder, session.paths.ava_config_dir / "command", UnifiedCommandSource::PromptGlobal,
                          "global");
}

void load_skill_commands(RegistryBuilder& builder, RuntimeSession const& session)
{
  auto loaded = ava::context::load_skills(ava::context::SkillLoadOptions{.workspace_root = session.workspace_dir});
  for (auto const& diagnostic : loaded.diagnostics) {
    add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::Skill),
                                                      .path = diagnostic.path,
                                                      .message = diagnostic.message});
  }
  for (auto const& skill : loaded.skills) {
    CommandRegistryEntry entry{.command = namespaced_command("skill", skill.name),
                               .description = skill.description,
                               .category = "Skills",
                               .source = UnifiedCommandSource::Skill,
                               .kind = UnifiedCommandKind::SkillPrompt,
                               .source_id = skill.name,
                               .source_path = skill.path,
                               .source_scope = ava::context::to_string(skill.source_type),
                               .template_text = skill.content,
                               .skill_name = skill.name};
    add_entry(builder, entry);
    entry.command = "/" + skill.name;
    add_entry(builder, std::move(entry));
  }
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options(RuntimeSession const& session)
{
  return ava::plugin::PluginDiscoveryOptions{.global_plugins_dir = session.paths.ava_config_dir / "plugins",
                                             .project_plugins_dir = session.workspace_dir / ".ava" / "plugins"};
}

std::filesystem::path plugin_enablement_file(RuntimeSession const& session)
{
  return session.paths.ava_state_dir / "plugin-enablement.json";
}

void load_plugin_commands(RegistryBuilder& builder, RuntimeSession const& session)
{
  auto diagnostics = ava::plugin::collect_plugin_diagnostics(plugin_discovery_options(session),
                                                             plugin_enablement_file(session), session.workspace_dir);
  for (auto const& failure : diagnostics.failures) {
    add_diagnostic(builder,
                   CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::PluginCommand),
                                             .path = failure.path,
                                             .message = failure.details.empty() ? failure.message : failure.details});
  }

  for (auto const& status : diagnostics.plugins) {
    auto const& manifest = status.plugin.manifest;
    for (auto const& command : manifest.contributes.commands) {
      if (!valid_command_segment(command.name) || !valid_command_segment(manifest.id)) {
        add_diagnostic(builder,
                       CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::PluginCommand),
                                                 .source_id = manifest.id,
                                                 .path = manifest.path,
                                                 .message = "plugin command id does not form a safe slash command"});
        continue;
      }
      CommandRegistryEntry entry{
          .command = namespaced_command("plugin", manifest.id, command.name),
          .description = command.description.empty() ? "Plugin command " + command.name : command.description,
          .category = "Plugins",
          .enabled = status.enabled,
          .disabled_reason = status.enabled ? "" : "plugin is disabled",
          .source = UnifiedCommandSource::PluginCommand,
          .kind = UnifiedCommandKind::PluginCommand,
          .source_id = manifest.id,
          .source_path = manifest.path,
          .source_scope = std::string(ava::plugin::to_string(status.plugin.scope)),
          .plugin_id = manifest.id,
          .plugin_command_name = command.name};
      add_entry(builder, entry);
      entry.command = "/" + command.name;
      add_entry(builder, std::move(entry));
    }
  }
}

std::string mcp_command_text(ava::mcp::McpServerConfig const& server)
{
  std::string text = server.command;
  for (auto const& arg : server.args) text += " " + arg;
  return text;
}

ava::core::VoidResult ensure_mcp_prompt_server_permission(ava::tools::ToolContext const& context,
                                                          ava::mcp::McpServerConfig const& server)
{
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch,
                                                      server.source_path, mcp_command_text(server), "mcp_prompts",
                                                      "MCP server launch requires permission");
      !permission) {
    return std::unexpected(std::move(permission.error()));
  }
  if (auto permission =
          ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server.source_path,
                                        server.id, "mcp_prompts", "MCP server connection requires permission");
      !permission) {
    return std::unexpected(std::move(permission.error()));
  }
  return {};
}

void load_mcp_prompt_commands(RegistryBuilder& builder, RuntimeSession& session, CommandRegistryOptions const& options)
{
  auto config_options = ava::mcp::default_mcp_config_options(session.workspace_dir);
  config_options.global_config_file = session.paths.ava_config_dir / "mcp.json";
  config_options.project_config_file = session.workspace_dir / ".ava" / "mcp.json";
  auto config = ava::mcp::load_mcp_config(config_options);
  if (!config) {
    add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                      .message = config.error().format()});
    return;
  }

  auto context = make_tool_context(session, options.permission_resolver);
  context.permission_tool_name = "mcp_prompts";
  context.current_tool_name = "mcp_prompts";
  context.cancel_requested = options.cancel_requested;

  for (auto const& server : config->servers) {
    if (!server.enabled) continue;
    if (!valid_command_segment(server.id)) {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = "MCP server id does not form a safe slash command"});
      continue;
    }
    if (auto permission = ensure_mcp_prompt_server_permission(context, server); !permission) {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = permission.error().format()});
      continue;
    }
    auto client = ava::mcp::McpStdioClient::start(
        server, ava::mcp::McpStdioClientOptions{.workspace_dir = session.workspace_dir}, options.cancel_requested);
    if (!client) {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = client.error().format()});
      continue;
    }
    auto prompts = (*client)->list_prompts(options.cancel_requested);
    auto shutdown = (*client)->shutdown();
    if (!prompts) {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = prompts.error().format()});
      continue;
    }
    if (!shutdown) {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = shutdown.error().format()});
      continue;
    }
    for (auto const& prompt : *prompts) {
      if (!valid_command_segment(prompt.name)) {
        add_diagnostic(builder,
                       CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                 .source_id = server.id,
                                                 .path = server.source_path,
                                                 .message = "MCP prompt name does not form a safe slash command"});
        continue;
      }
      CommandRegistryEntry entry{
          .command = namespaced_command("mcp", server.id, prompt.name),
          .description = prompt.description.empty() ? "MCP prompt " + prompt.name : prompt.description,
          .category = "MCP",
          .source = UnifiedCommandSource::McpPrompt,
          .kind = UnifiedCommandKind::McpPrompt,
          .source_id = server.id,
          .source_path = server.source_path,
          .source_scope = std::string(ava::mcp::to_string(server.scope)),
          .mcp_server_id = server.id,
          .mcp_prompt_name = prompt.name,
          .mcp_arguments = prompt.arguments};
      std::vector<std::string> required;
      for (auto const& argument : prompt.arguments) {
        if (argument.required) required.push_back(argument.name);
      }
      if (!prompt.arguments.empty())
        entry.hint = "<" + joined_strings(required.empty() ? std::vector<std::string>{"args"} : required, "> <") + ">";
      add_entry(builder, entry);
      entry.command = "/" + prompt.name;
      add_entry(builder, std::move(entry));
    }
  }
}

ava::core::Result<std::vector<std::string>> parse_argument_tokens(std::string_view text)
{
  std::vector<std::string> tokens;
  std::string current;
  std::optional<char> quote;
  bool escaped = false;
  bool in_token = false;
  for (char const ch : text) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      if (ch != '\t' && ch != '\r' && ch != '\n') {
        return std::unexpected(
            ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command arguments contain control bytes"));
      }
    }
    if (escaped) {
      current.push_back(ch);
      escaped = false;
      in_token = true;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      in_token = true;
      continue;
    }
    if (quote) {
      if (ch == *quote) {
        quote.reset();
      } else {
        current.push_back(ch);
      }
      in_token = true;
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quote = ch;
      in_token = true;
      continue;
    }
    if (std::isspace(byte) != 0) {
      if (in_token) {
        tokens.push_back(std::move(current));
        current.clear();
        in_token = false;
      }
      continue;
    }
    current.push_back(ch);
    in_token = true;
  }
  if (escaped) current.push_back('\\');
  if (quote) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command arguments contain an unterminated quote"));
  }
  if (in_token) tokens.push_back(std::move(current));
  return tokens;
}

std::string join_tokens(std::vector<std::string> const& tokens, std::size_t start, std::optional<std::size_t> count)
{
  if (start >= tokens.size()) return {};
  auto end = tokens.size();
  if (count) end = std::min(end, start + *count);
  std::string joined;
  for (std::size_t index = start; index < end; ++index) {
    if (!joined.empty()) joined += ' ';
    joined += tokens[index];
  }
  return joined;
}

std::optional<std::pair<std::size_t, std::optional<std::size_t>>> parse_argument_slice(std::string_view value)
{
  if (!value.starts_with("${@:") || !value.ends_with('}')) return std::nullopt;
  value.remove_prefix(4);
  value.remove_suffix(1);
  auto const colon = value.find(':');
  auto parse_size = [](std::string_view text) -> std::optional<std::size_t> {
    if (text.empty()) return std::nullopt;
    std::size_t value = 0;
    for (char const ch : text) {
      if (ch < '0' || ch > '9') return std::nullopt;
      value = value * 10 + static_cast<std::size_t>(ch - '0');
    }
    return value;
  };
  auto start = parse_size(colon == std::string_view::npos ? value : value.substr(0, colon));
  if (!start || *start == 0) return std::nullopt;
  if (colon == std::string_view::npos)
    return std::pair<std::size_t, std::optional<std::size_t>>{*start - 1, std::nullopt};
  auto count = parse_size(value.substr(colon + 1));
  if (!count) return std::nullopt;
  return std::pair<std::size_t, std::optional<std::size_t>>{*start - 1, *count};
}

}  // namespace

std::string to_string(UnifiedCommandSource source)
{
  switch (source) {
    case UnifiedCommandSource::Builtin:
      return "builtin";
    case UnifiedCommandSource::PromptProject:
      return "prompt_project";
    case UnifiedCommandSource::PromptGlobal:
      return "prompt_global";
    case UnifiedCommandSource::Skill:
      return "skill";
    case UnifiedCommandSource::McpPrompt:
      return "mcp_prompt";
    case UnifiedCommandSource::PluginCommand:
      return "plugin_command";
  }
  return "unknown";
}

std::string to_string(UnifiedCommandKind kind)
{
  switch (kind) {
    case UnifiedCommandKind::Backend:
      return "backend";
    case UnifiedCommandKind::PromptTemplate:
      return "prompt_template";
    case UnifiedCommandKind::SkillPrompt:
      return "skill_prompt";
    case UnifiedCommandKind::McpPrompt:
      return "mcp_prompt";
    case UnifiedCommandKind::PluginCommand:
      return "plugin_command";
  }
  return "unknown";
}

CommandRegistry load_command_registry(RuntimeSession& session, CommandRegistryOptions options)
{
  RegistryBuilder builder;
  if (options.include_builtins) load_builtin_commands(builder);
  if (options.include_prompt_commands) load_prompt_commands(builder, session);
  if (options.include_plugin_commands) load_plugin_commands(builder, session);
  if (options.include_mcp_prompts) load_mcp_prompt_commands(builder, session, options);
  if (options.include_skills) load_skill_commands(builder, session);
  return std::move(builder.registry);
}

CommandRegistryEntry const* find_command_registry_entry(CommandRegistry const& registry, std::string_view line) noexcept
{
  if (!line.starts_with('/')) return nullptr;
  auto const token = command_token(line);
  for (auto const& entry : registry.entries) {
    if (entry.command == token) return &entry;
    if (std::ranges::find(entry.aliases, token) != entry.aliases.end()) return &entry;
  }
  return nullptr;
}

bool command_registry_contains(RuntimeSession& session, std::string_view line)
{
  if (!line.starts_with('/')) return false;
  auto const token = command_token(line);
  if (token.starts_with("/skill:") || token.starts_with("/mcp:") || token.starts_with("/plugin:")) return true;
  auto registry = load_command_registry(session, CommandRegistryOptions{.include_builtins = true,
                                                                        .include_prompt_commands = true,
                                                                        .include_skills = true,
                                                                        .include_plugin_commands = true,
                                                                        .include_mcp_prompts = false});
  if (find_command_registry_entry(registry, line) != nullptr) return true;
  registry = load_command_registry(session, CommandRegistryOptions{.include_builtins = false,
                                                                   .include_prompt_commands = false,
                                                                   .include_skills = false,
                                                                   .include_plugin_commands = false,
                                                                   .include_mcp_prompts = true});
  return find_command_registry_entry(registry, line) != nullptr;
}

ava::core::Result<std::string> expand_prompt_command_template(std::string_view template_text,
                                                              std::string_view argument_text)
{
  auto tokens = parse_argument_tokens(argument_text);
  if (!tokens) return std::unexpected(std::move(tokens.error()));
  auto const raw_arguments = trim(argument_text);

  std::string output;
  for (std::size_t index = 0; index < template_text.size();) {
    if (template_text[index] != '$') {
      output.push_back(template_text[index++]);
      continue;
    }
    if (template_text.substr(index, 10) == "$ARGUMENTS") {
      output += raw_arguments;
      index += 10;
      continue;
    }
    if (template_text.substr(index, 2) == "$@") {
      output += join_tokens(*tokens, 0, std::nullopt);
      index += 2;
      continue;
    }
    if (index + 1 < template_text.size() && std::isdigit(static_cast<unsigned char>(template_text[index + 1])) != 0) {
      std::size_t end = index + 1;
      std::size_t number = 0;
      while (end < template_text.size() && std::isdigit(static_cast<unsigned char>(template_text[end])) != 0) {
        number = number * 10 + static_cast<std::size_t>(template_text[end] - '0');
        ++end;
      }
      if (number > 0 && number <= tokens->size()) output += (*tokens)[number - 1];
      index = end;
      continue;
    }
    if (template_text.substr(index, 4) == "${@:") {
      auto const close = template_text.find('}', index);
      if (close != std::string_view::npos) {
        auto const expression = template_text.substr(index, close - index + 1);
        if (auto slice = parse_argument_slice(expression)) {
          output += join_tokens(*tokens, slice->first, slice->second);
          index = close + 1;
          continue;
        }
      }
    }
    if (template_text.substr(index, 2) == "$$") {
      output.push_back('$');
      index += 2;
      continue;
    }
    output.push_back(template_text[index++]);
  }
  return output;
}

ava::core::Result<std::string> mcp_prompt_arguments_json(CommandRegistryEntry const& entry,
                                                         std::string_view argument_text)
{
  auto const trimmed = trim_view(argument_text);
  if (!trimmed.empty() && trimmed.front() == '{') {
    auto json = std::string(trimmed);
    if (!ava::core::json::is_valid_object(json)) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "MCP prompt arguments must be a JSON object"));
    }
    return json;
  }

  auto tokens = parse_argument_tokens(argument_text);
  if (!tokens) return std::unexpected(std::move(tokens.error()));
  if (tokens->size() > entry.mcp_arguments.size()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "too many MCP prompt arguments"));
  }
  for (std::size_t index = 0; index < entry.mcp_arguments.size(); ++index) {
    if (entry.mcp_arguments[index].required && index >= tokens->size()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "missing required MCP prompt argument");
      error.with_context("argument", entry.mcp_arguments[index].name);
      return std::unexpected(std::move(error));
    }
  }

  std::string json = "{";
  for (std::size_t index = 0; index < tokens->size(); ++index) {
    if (index > 0) json += ',';
    json += "\"" + ava::core::json::escape(entry.mcp_arguments[index].name) + "\":\"" +
            ava::core::json::escape((*tokens)[index]) + "\"";
  }
  json += '}';
  return json;
}

}  // namespace ava::app
