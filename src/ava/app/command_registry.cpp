#include "sys.h"
#include "ava/app/command_format.h"
#include "ava/app/command_registry.h"
#include "ava/app/command_tools.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime/Session.h"
#include "ava/tools/file_tools.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/static_resources.h"
#include "ava/mcp/config.h"
#include "ava/context/skill_loader.h"
#include "ava/core/fingerprint.h"
#include "ava/core/json.h"
#include "ava/core/string_utils.h"

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

std::string_view command_token(std::string_view line) noexcept
{
  auto const end = line.find_first_of(" \t\r\n");
  return line.substr(0, end == std::string_view::npos ? line.size() : end);
}

ava::core::Result<std::vector<std::string>> parse_argument_tokens(std::string_view text)
{
  std::vector<std::string> tokens;
  std::string current;
  std::optional<char> quote;
  bool escaped = false;
  bool in_token = false;
  for (char const ch : text)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
    {
      if (ch != '\t' && ch != '\r' && ch != '\n')
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command arguments contain control bytes"));
      }
    }
    if (escaped)
    {
      current.push_back(ch);
      escaped = false;
      in_token = true;
      continue;
    }
    if (ch == '\\')
    {
      escaped = true;
      in_token = true;
      continue;
    }
    if (quote)
    {
      if (ch == *quote)
      {
        quote.reset();
      }
      else
      {
        current.push_back(ch);
      }
      in_token = true;
      continue;
    }
    if (ch == '"' || ch == '\'')
    {
      quote = ch;
      in_token = true;
      continue;
    }
    if (std::isspace(byte) != 0)
    {
      if (in_token)
      {
        tokens.push_back(std::move(current));
        current.clear();
        in_token = false;
      }
      continue;
    }
    current.push_back(ch);
    in_token = true;
  }
  if (escaped)
    current.push_back('\\');
  if (quote)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command arguments contain an unterminated quote"));
  }
  if (in_token)
    tokens.push_back(std::move(current));
  return tokens;
}

std::string join_tokens(std::vector<std::string> const& tokens, std::size_t start, std::optional<std::size_t> count)
{
  if (start >= tokens.size())
    return {};
  auto end = tokens.size();
  if (count)
    end = std::min(end, start + *count);
  std::string joined;
  for (std::size_t index = start; index < end; ++index)
  {
    if (!joined.empty())
      joined += ' ';
    joined += tokens[index];
  }
  return joined;
}

std::optional<std::pair<std::size_t, std::optional<std::size_t>>> parse_argument_slice(std::string_view value)
{
  if (!value.starts_with("${@:") || !value.ends_with('}'))
    return std::nullopt;
  value.remove_prefix(4);
  value.remove_suffix(1);
  auto const colon = value.find(':');
  auto parse_size = [](std::string_view text) -> std::optional<std::size_t> {
    if (text.empty())
      return std::nullopt;
    std::size_t value = 0;
    for (char const ch : text)
    {
      if (ch < '0' || ch > '9')
        return std::nullopt;
      value = value * 10 + static_cast<std::size_t>(ch - '0');
    }
    return value;
  };
  auto start = parse_size(colon == std::string_view::npos ? value : value.substr(0, colon));
  if (!start || *start == 0)
    return std::nullopt;
  if (colon == std::string_view::npos)
    return std::pair<std::size_t, std::optional<std::size_t>>{*start - 1, std::nullopt};
  auto count = parse_size(value.substr(colon + 1));
  if (!count)
    return std::nullopt;
  return std::pair<std::size_t, std::optional<std::size_t>>{*start - 1, *count};
}

std::optional<std::pair<std::size_t, std::string_view>> parse_argument_default(std::string_view value)
{
  if (!value.starts_with("${") || !value.ends_with('}'))
    return std::nullopt;
  value.remove_prefix(2);
  value.remove_suffix(1);
  if (value.empty() || value.front() < '1' || value.front() > '9')
    return std::nullopt;

  std::size_t index = 0;
  std::size_t number = 0;
  while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index])) != 0)
  {
    number = number * 10 + static_cast<std::size_t>(value[index] - '0');
    ++index;
  }
  if (number == 0 || value.substr(index, 2) != ":-")
    return std::nullopt;
  return std::pair<std::size_t, std::string_view>{number - 1, value.substr(index + 2)};
}

}  // namespace

std::string to_string(UnifiedCommandSource source)
{
  switch (source)
  {
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
  switch (kind)
  {
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

CommandRegistryEntry const* find_command_registry_entry(CommandRegistry const& registry, std::string_view line) noexcept
{
  if (!line.starts_with('/'))
    return nullptr;
  auto const token = command_token(line);
  for (auto const& entry : registry.entries)
  {
    if (entry.command == token)
      return &entry;
    if (std::ranges::find(entry.aliases, token) != entry.aliases.end())
      return &entry;
  }
  return nullptr;
}

bool command_registry_contains(runtime::session_ts& unlocked_session, std::string_view line)
{
  if (!line.starts_with('/'))
    return false;
  auto const token = command_token(line);
  if (token.starts_with("/skill:") || token.starts_with("/mcp:") || token.starts_with("/plugin:"))
    return true;

  SCOPED_CRITICAL_AREA_W(session_w, unlocked_session);
  auto registry = session_w->load_command_registry(
      CommandRegistryOptions{
          .include_builtins = true, .include_prompt_commands = true, .include_skills = true, .include_plugin_commands = true, .include_mcp_prompts = false});
  if (find_command_registry_entry(registry, line) != nullptr)
    return true;
  registry = session_w->load_command_registry(
      CommandRegistryOptions{
          .include_builtins = false, .include_prompt_commands = false, .include_skills = false, .include_plugin_commands = false, .include_mcp_prompts = true});
  return find_command_registry_entry(registry, line) != nullptr;
}

ava::core::Result<std::string> expand_prompt_command_template(std::string_view template_text, std::string_view argument_text)
{
  auto tokens = parse_argument_tokens(argument_text);
  if (!tokens)
    return std::unexpected(std::move(tokens.error()));
  auto const raw_arguments = core::trim(argument_text);

  std::string output;
  for (std::size_t index = 0; index < template_text.size();)
  {
    if (template_text[index] != '$')
    {
      output.push_back(template_text[index++]);
      continue;
    }
    if (template_text.substr(index, 10) == "$ARGUMENTS")
    {
      output += raw_arguments;
      index += 10;
      continue;
    }
    if (template_text.substr(index, 2) == "$@")
    {
      output += join_tokens(*tokens, 0, std::nullopt);
      index += 2;
      continue;
    }
    if (index + 1 < template_text.size() && std::isdigit(static_cast<unsigned char>(template_text[index + 1])) != 0)
    {
      std::size_t end = index + 1;
      std::size_t number = 0;
      while (end < template_text.size() && std::isdigit(static_cast<unsigned char>(template_text[end])) != 0)
      {
        number = number * 10 + static_cast<std::size_t>(template_text[end] - '0');
        ++end;
      }
      if (number > 0 && number <= tokens->size())
        output += (*tokens)[number - 1];
      index = end;
      continue;
    }
    if (template_text.substr(index, 4) == "${@:")
    {
      auto const close = template_text.find('}', index);
      if (close != std::string_view::npos)
      {
        auto const expression = template_text.substr(index, close - index + 1);
        if (auto slice = parse_argument_slice(expression))
        {
          output += join_tokens(*tokens, slice->first, slice->second);
          index = close + 1;
          continue;
        }
      }
    }
    if (template_text.substr(index, 2) == "${")
    {
      auto const close = template_text.find('}', index);
      if (close != std::string_view::npos)
      {
        auto const expression = template_text.substr(index, close - index + 1);
        if (auto default_value = parse_argument_default(expression))
        {
          if (default_value->first < tokens->size() && !(*tokens)[default_value->first].empty())
            output += (*tokens)[default_value->first];
          else
            output += default_value->second;
          index = close + 1;
          continue;
        }
      }
    }
    if (template_text.substr(index, 2) == "$$")
    {
      output.push_back('$');
      index += 2;
      continue;
    }
    output.push_back(template_text[index++]);
  }
  return output;
}

ava::core::Result<std::string> mcp_prompt_arguments_json(CommandRegistryEntry const& entry, std::string_view argument_text)
{
  auto const trimmed = core::trim_view(argument_text);
  if (!trimmed.empty() && trimmed.front() == '{')
  {
    auto json = std::string(trimmed);
    if (!ava::core::json::is_valid_object(json))
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "MCP prompt arguments must be a JSON object"));
    }
    return json;
  }

  auto tokens = parse_argument_tokens(argument_text);
  if (!tokens)
    return std::unexpected(std::move(tokens.error()));
  if (tokens->size() > entry.mcp_arguments.size())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "too many MCP prompt arguments"));
  }
  for (std::size_t index = 0; index < entry.mcp_arguments.size(); ++index)
  {
    if (entry.mcp_arguments[index].required && index >= tokens->size())
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "missing required MCP prompt argument");
      error.with_context("argument", entry.mcp_arguments[index].name);
      return std::unexpected(std::move(error));
    }
  }

  std::string json = "{";
  for (std::size_t index = 0; index < tokens->size(); ++index)
  {
    if (index > 0)
      json += ',';
    json += "\"" + ava::core::json::escape(entry.mcp_arguments[index].name) + "\":\"" + ava::core::json::escape((*tokens)[index]) + "\"";
  }
  json += '}';
  return json;
}

}  // namespace ava::app
