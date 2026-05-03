#include "ava/app/commands.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "ava/config/auth.h"
#include "ava/core/ids.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/stats.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/keybindings.h"

namespace ava::app {
namespace {

void add_output(CommandResult& result, std::string text) { result.output.push_back(std::move(text)); }

std::vector<CommandHotkey> default_command_hotkeys() {
  std::vector<CommandHotkey> hotkeys;
  for (const auto& item : ava::tui::key_binding_help_items(ava::tui::default_key_bindings())) {
    hotkeys.push_back(CommandHotkey{.action = item.action, .description = item.description, .keys = item.keys});
  }
  return hotkeys;
}

std::vector<CommandHotkey> effective_hotkeys(const std::vector<CommandHotkey>& hotkeys) {
  return hotkeys.empty() ? default_command_hotkeys() : hotkeys;
}

std::string aliases_text(const CommandCatalogEntry& entry) {
  std::string text;
  for (const auto& alias : entry.aliases) {
    if (!text.empty()) text += ", ";
    text += alias;
  }
  return text;
}

std::string command_display(const CommandCatalogEntry& entry) {
  auto text = entry.command;
  if (!entry.hint.empty()) text += " " + entry.hint;
  const auto aliases = aliases_text(entry);
  if (!aliases.empty()) text += " (alias: " + aliases + ")";
  return text;
}

std::string command_rows(bool enabled) {
  std::size_t width = 0;
  std::vector<const CommandCatalogEntry*> entries;
  for (const auto& entry : command_catalog()) {
    if (entry.enabled != enabled) continue;
    entries.push_back(&entry);
    width = std::max(width, command_display(entry).size());
  }

  std::string output;
  for (const auto* entry : entries) {
    auto display = command_display(*entry);
    output += "  " + display;
    if (display.size() < width) output += std::string(width - display.size(), ' ');
    output += "  " + entry->description;
    if (!entry->enabled && !entry->disabled_reason.empty()) output += " — disabled: " + entry->disabled_reason;
    output += '\n';
  }
  return output;
}

std::string display_path(const std::filesystem::path& path, const std::filesystem::path& base) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, base, error);
  if (!error) return relative.generic_string();
  return path.generic_string();
}

ava::tools::ToolContext make_tool_context(RuntimeSession& session,
                                          ava::permissions::PermissionResolver permission_resolver) {
  return ava::tools::ToolContext{
      .workspace_dir = session.workspace_dir,
      .spill_dir = session.store.session_path().parent_path() / "spill",
      .mode = session.mode,
      .permission_resolver = std::move(permission_resolver),
      .permission_audit_sink =
          [&store = session.store](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
        return store.append(ava::session::SessionEntry{
            .id = ava::core::make_id("entry"),
            .parent_id = "",
            .type = ava::session::EntryType::PermissionDecision,
            .timestamp = ava::session::now_timestamp(),
            .data_json = ava::tools::permission_audit_data_json(event),
        });
      }};
}

ava::core::VoidResult append_mode_change(ava::session::SessionStore& store, ava::agent::Mode mode) {
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::ModeChange,
      .timestamp = ava::session::now_timestamp(),
      .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}",
  });
}

RuntimeEvent command_event(const RuntimeSession& session, RuntimeEventType type) {
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

ava::core::VoidResult emit_tool_event(const RuntimeSession& session, const RuntimeEventSink& sink,
                                      const ava::agent::ToolTimelineEntry& entry) {
  auto event =
      command_event(session, entry.status == ava::agent::ToolTimelineStatus::Running ? RuntimeEventType::ToolStart
                                                                                     : RuntimeEventType::ToolResult);
  event.call_id = entry.call_id;
  event.tool_name = entry.name;
  event.status = ava::agent::to_string(entry.status);
  event.text = entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary : entry.result_summary;
  return emit_event(sink, event);
}

ava::core::VoidResult record_tool_event(const RuntimeSession& session, const RuntimeEventSink& sink,
                                        CommandResult& result, ava::agent::ToolTimelineEntry entry) {
  if (auto emitted = emit_tool_event(session, sink, entry); !emitted)
    return std::unexpected(std::move(emitted.error()));
  result.tool_timeline.push_back(std::move(entry));
  return {};
}

ava::core::VoidResult record_tool_start(const RuntimeSession& session, const RuntimeEventSink& sink,
                                        CommandResult& result, const std::string& call_id, std::string name,
                                        std::string argument_summary) {
  return record_tool_event(session, sink, result,
                           ava::agent::ToolTimelineEntry{.status = ava::agent::ToolTimelineStatus::Running,
                                                         .call_id = call_id,
                                                         .name = std::move(name),
                                                         .argument_summary = std::move(argument_summary)});
}

ava::core::VoidResult record_tool_result(const RuntimeSession& session, const RuntimeEventSink& sink,
                                         CommandResult& result, const std::string& call_id, std::string name,
                                         ava::agent::ToolTimelineStatus status, std::string result_summary) {
  return record_tool_event(
      session, sink, result,
      ava::agent::ToolTimelineEntry{
          .status = status, .call_id = call_id, .name = std::move(name), .result_summary = std::move(result_summary)});
}

bool starts_with_command(std::string_view line, std::string_view command) noexcept {
  return line == command || (line.starts_with(command) && line.size() > command.size() && line[command.size()] == ' ');
}

std::string missing_argument(std::string_view usage) { return "usage: " + std::string(usage); }

std::string command_argument(std::string_view line, std::string_view command) {
  if (line.size() <= command.size() || line[command.size()] != ' ') return {};
  return std::string(line.substr(command.size() + 1));
}

std::vector<std::string> split_command_arguments(std::string_view text) {
  std::vector<std::string> parts;
  std::size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
    const auto start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0) ++index;
    if (start < index) parts.emplace_back(text.substr(start, index - start));
  }
  return parts;
}

bool is_valid_connect_provider_id(std::string_view provider_id) {
  if (provider_id.empty() || provider_id.size() > 128) return false;
  return std::ranges::all_of(provider_id, [](char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || ch == '-' || ch == '_';
  });
}

std::string trim_secret_text(std::string secret) {
  auto is_edge_space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
  auto first = std::find_if_not(secret.begin(), secret.end(), is_edge_space);
  auto last = std::find_if_not(secret.rbegin(), secret.rend(), is_edge_space).base();
  if (first >= last) return {};
  return std::string(first, last);
}

std::string format_cost_usd(long double value) {
  std::ostringstream output;
  output << '$' << std::fixed << std::setprecision(6) << value;
  return output.str();
}

template <typename Value>
void append_known_value(std::ostringstream& output, bool& wrote_any, std::string_view label,
                        const std::optional<Value>& value) {
  if (!value) return;
  if (wrote_any) output << ' ';
  output << label << '=' << *value;
  wrote_any = true;
}

std::string shorten_middle(std::string text, std::size_t max_columns) {
  if (text.size() <= max_columns || max_columns < 8) return text;
  const auto front = (max_columns - 3) / 2;
  const auto back = max_columns - 3 - front;
  return text.substr(0, front) + "..." + text.substr(text.size() - back);
}

std::string compact_workspace_label(const std::filesystem::path& workspace) {
  const auto filename = workspace.filename().generic_string();
  if (!filename.empty()) return shorten_middle(filename, 32);
  return shorten_middle(workspace.generic_string(), 48);
}

std::string compact_cwd_label(const std::filesystem::path& cwd, const std::filesystem::path& workspace) {
  auto text = display_path(cwd, workspace);
  if (text.empty()) text = ".";
  return shorten_middle(std::move(text), 48);
}

std::string known_values_text(const ava::session::SessionStats& stats) {
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.input_tokens);
  append_known_value(output, wrote_any, "output", stats.output_tokens);
  append_known_value(output, wrote_any, "reasoning", stats.reasoning_tokens);
  append_known_value(output, wrote_any, "cache_read", stats.cache_read_tokens);
  append_known_value(output, wrote_any, "cache_write", stats.cache_write_tokens);
  append_known_value(output, wrote_any, "total", stats.total_tokens);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string estimated_bytes_text(const ava::session::SessionStats& stats) {
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.estimated_input_bytes);
  append_known_value(output, wrote_any, "output", stats.estimated_output_bytes);
  append_known_value(output, wrote_any, "total", stats.estimated_total_bytes);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string cost_text(const ava::session::SessionStats& stats) {
  if (stats.cost_complete) return stats.total_cost_usd ? format_cost_usd(*stats.total_cost_usd) : "unavailable";
  if (stats.known_cost_usd) {
    return "at least " + format_cost_usd(*stats.known_cost_usd) + " (" + std::to_string(stats.unknown_cost_entries) +
           " unknown)";
  }
  return "incomplete (" + std::to_string(stats.unknown_cost_entries) + " unknown)";
}

std::string format_session_stats_text(const RuntimeSession& session, const ava::session::SessionStats& stats) {
  std::ostringstream output;
  output << "Session stats\n";
  output << "  session: " << shorten_middle(session.store.session_id(), 32) << "   entries: " << stats.entry_count
         << '\n';
  output << "  model: " << session.model.provider_id << '/' << session.model.model_id
         << "   mode: " << ava::agent::to_string(session.mode) << '\n';
  output << "  workspace: " << compact_workspace_label(session.workspace_dir)
         << "   cwd: " << compact_cwd_label(session.current_dir, session.workspace_dir) << '\n';
  if (!stats.first_timestamp.empty() || !stats.last_timestamp.empty()) {
    output << "  time: " << (stats.first_timestamp.empty() ? "unknown" : stats.first_timestamp) << " -> "
           << (stats.last_timestamp.empty() ? "unknown" : stats.last_timestamp) << '\n';
  }

  output << "\nMessages:\n";
  output << "  user " << stats.counts.user_message << "   assistant " << stats.counts.assistant_message << "   tools "
         << stats.counts.tool_call << '/' << stats.counts.tool_result << "   permissions "
         << stats.counts.permission_decision << '\n';
  output << "  compactions " << stats.counts.compaction << "   mode/model " << stats.counts.mode_change << '/'
         << stats.counts.model_change << "   errors/cancels " << stats.counts.error << '/' << stats.counts.cancel
         << '\n';

  output << "\nUsage:\n";
  output << "  tokens: " << known_values_text(stats) << '\n';
  output << "  est bytes: " << estimated_bytes_text(stats) << '\n';
  output << "  cost: " << cost_text(stats) << "   usage entries exact/estimated " << stats.exact_usage_entries << '/'
         << stats.estimated_usage_entries << '\n';

  output << "\nHints:\n";
  output << "  export: /export   resume: ava --session " << session.store.session_id();
  return output.str();
}

std::string credential_type_value(std::string_view method) {
  if (method == "api" || method == "api-key" || method == "apikey" || method == "key" || method == "api_key") {
    return "api_key";
  }
  if (method == "oauth" || method == "oauth-token" || method == "oauth_token" || method == "bearer" ||
      method == "token") {
    return "oauth";
  }
  return {};
}

std::string credential_type_label(std::string_view credential_type) {
  return credential_type == "oauth" ? "OAuth bearer token" : "API key";
}

std::string selected_or_custom_answer(const ava::agent::QuestionAnswer& answer) {
  if (!answer.custom_text.empty()) return answer.custom_text;
  if (!answer.selected_options.empty()) return answer.selected_options.front();
  return {};
}

ava::core::Result<ava::agent::QuestionAnswer> ask_connect_question(const CommandRequest& request,
                                                                   ava::agent::QuestionPrompt prompt) {
  if (!request.question_resolver) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                         "/connect requires the interactive TUI; use `ava connect <provider> --api-key-stdin`, "
                         "`--api-key-env ENV`, `--oauth-token-stdin`, or `--oauth-token-env ENV` for headless setup"));
  }
  return request.question_resolver(prompt);
}

std::vector<ava::agent::QuestionOption> provider_options(const RuntimeSession& session) {
  std::vector<ava::agent::QuestionOption> options;
  auto add = [&](std::string value, std::string label) {
    if (std::ranges::any_of(options, [&](const auto& option) { return option.value == value; })) return;
    options.push_back(ava::agent::QuestionOption{.value = std::move(value), .label = std::move(label)});
  };
  if (!session.model.provider_id.empty()) add(session.model.provider_id, session.model.provider_id + " (current)");
  add("openai", "OpenAI - API key or ChatGPT OAuth token");
  add("anthropic", "Anthropic - Claude API key or OAuth token");
  add("moonshot", "Moonshot - Kimi API key");
  add("kimi", "Kimi - Moonshot compatible API key");
  add("openrouter", "OpenRouter - API key");
  add("vercel", "Vercel AI Gateway - API key");
  return options;
}

ava::core::Result<std::string> resolve_connect_provider(RuntimeSession& session, const CommandRequest& request,
                                                        const std::vector<std::string>& args) {
  if (!args.empty()) return args[0];
  auto answer = ask_connect_question(request, ava::agent::QuestionPrompt{.header = "Connect a provider",
                                                                         .question = "Select provider",
                                                                         .options = provider_options(session),
                                                                         .multiple = false,
                                                                         .allow_custom = true,
                                                                         .secret = false,
                                                                         .modal = true,
                                                                         .searchable = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  return selected_or_custom_answer(*answer);
}

ava::core::Result<std::string> resolve_connect_credential_type(const CommandRequest& request,
                                                               const std::vector<std::string>& args) {
  if (args.size() >= 2) {
    const auto credential_type = credential_type_value(args[1]);
    if (!credential_type.empty()) return credential_type;
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "/connect credential type must be api-key or oauth");
    error.with_context("usage", "/connect [provider] [api-key|oauth]");
    return std::unexpected(std::move(error));
  }

  auto answer = ask_connect_question(
      request, ava::agent::QuestionPrompt{
                   .header = "Connect a provider",
                   .question = "Choose credential type",
                   .options = {ava::agent::QuestionOption{.value = "api_key", .label = "API key"},
                               ava::agent::QuestionOption{.value = "oauth", .label = "OAuth bearer token"}},
                   .multiple = false,
                   .allow_custom = false,
                   .secret = false,
                   .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  return selected_or_custom_answer(*answer);
}

ava::core::Result<std::string> prompt_connect_secret(const CommandRequest& request, std::string_view provider_id,
                                                     std::string_view credential_type) {
  auto answer = ask_connect_question(
      request, ava::agent::QuestionPrompt{
                   .header = "Connect a provider",
                   .question = "Paste " + credential_type_label(credential_type) + " for " + std::string(provider_id),
                   .options = {},
                   .multiple = false,
                   .allow_custom = true,
                   .secret = true,
                   .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  auto secret = trim_secret_text(selected_or_custom_answer(*answer));
  if (secret.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "credential was empty"));
  }
  return secret;
}

ava::core::Result<CommandResult> run_connect_command(RuntimeSession& session, const CommandRequest& request) {
  CommandResult result;
  result.handled = true;
  const auto args = split_command_arguments(command_argument(request.command, "/connect"));
  if (args.size() > 2) {
    add_output(result, missing_argument("/connect [provider] [api-key|oauth]"));
    return result;
  }

  auto provider_id = resolve_connect_provider(session, request, args);
  if (!provider_id) {
    add_output(result, provider_id.error().format());
    return result;
  }
  if (!is_valid_connect_provider_id(*provider_id)) {
    add_output(result, "provider id must contain only letters, numbers, '-' or '_'");
    return result;
  }

  auto credential_type = resolve_connect_credential_type(request, args);
  if (!credential_type) {
    add_output(result, credential_type.error().format());
    return result;
  }

  auto secret = prompt_connect_secret(request, *provider_id, *credential_type);
  if (!secret) {
    add_output(result, secret.error().format());
    return result;
  }

  auto stored = ava::config::store_provider_credential(
      session.paths, ava::config::ProviderCredential{.provider_id = *provider_id,
                                                     .access_token = *secret,
                                                     .credential_type = *credential_type,
                                                     .account_id = "",
                                                     .source = "connect"});
  if (!stored) {
    add_output(result, stored.error().format());
    return result;
  }
  add_output(result, "Stored " + *provider_id + " " + credential_type_label(*credential_type) + " credential at " +
                         session.paths.auth_file.string());
  return result;
}

ava::core::Result<CommandResult> run_tool_command(RuntimeSession& session, CommandRequest& request) {
  CommandResult result;
  result.handled = true;
  const auto& line = request.command;
  auto context = make_tool_context(session, request.permission_resolver);

  if (line.starts_with("/read ")) {
    const auto argument = line.substr(6);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "read", argument); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto output = ava::tools::read_file(context, session.current_dir / argument);
    if (!output) {
      const auto text = output.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "read",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string text = output->content;
    if (output->truncated) {
      text += "\n[truncated " + std::to_string(output->output_bytes) + '/' + std::to_string(output->total_bytes) +
              " bytes]";
    }
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "read",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(output->output_bytes) + " bytes");
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(text));
    return result;
  }

  if (line.starts_with("/glob ")) {
    const auto pattern = line.substr(6);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "glob", pattern); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto glob = ava::tools::glob_files(context, pattern);
    if (!glob) {
      const auto text = glob.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "glob",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output;
    for (const auto& path : glob->paths) output += display_path(path, session.current_dir) + '\n';
    if (glob->truncated) {
      output += "[truncated " + std::to_string(glob->paths.size()) + '/' + std::to_string(glob->total_matches) +
                " matches]\n";
    }
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "glob",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(glob->paths.size()) + " matches");
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  if (line.starts_with("/grep ")) {
    const auto rest = line.substr(6);
    const auto split = rest.find(' ');
    const auto pattern = split == std::string::npos ? rest : rest.substr(0, split);
    const auto include = split == std::string::npos ? std::string("**/*") : rest.substr(split + 1);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "grep", pattern); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto grep = ava::tools::grep_files(context, pattern, include);
    if (!grep) {
      const auto text = grep.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "grep",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output;
    for (const auto& match : grep->matches) {
      output +=
          display_path(match.path, session.current_dir) + ':' + std::to_string(match.line_number) + ": " + match.line;
      if (match.line_truncated) output += " [line truncated]";
      output += '\n';
    }
    if (grep->truncated) {
      output += "[truncated " + std::to_string(grep->matches.size()) + '/' + std::to_string(grep->total_matches) +
                " matches]\n";
    }
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "grep",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(grep->matches.size()) + " matches");
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  if (line.starts_with("/write ")) {
    const auto rest = line.substr(7);
    const auto split = rest.find(' ');
    if (split == std::string::npos) {
      add_output(result, missing_argument("/write <path> <text>"));
      return result;
    }
    const auto path_text = rest.substr(0, split);
    const auto text = rest.substr(split + 1);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "write", path_text);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto write = ava::tools::write_file(context, session.current_dir / path_text, text);
    if (!write) {
      const auto error_text = write.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "write",
                                             ava::agent::ToolTimelineStatus::Error, error_text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, error_text);
      return result;
    }
    const auto output = "wrote " + std::to_string(write->bytes_written) + " bytes to " + write->path.string();
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "write",
                                           ava::agent::ToolTimelineStatus::Success, output);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, output);
    return result;
  }

  if (line.starts_with("/bash ")) {
    const auto command = line.substr(6);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "bash", command); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto bash = ava::tools::run_bash(context, command);
    if (!bash) {
      const auto text = bash.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "bash",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output = "exit: " + std::to_string(bash->exit_code);
    if (bash->timed_out) output += " (timed out)";
    if (bash->truncated) {
      output += " (output truncated to last " + std::to_string(bash->output.size()) + '/' +
                std::to_string(bash->total_bytes) + " bytes)";
    }
    output += '\n' + bash->output;
    if (auto recorded = record_tool_result(
            session, request.event_sink, result, call_id, "bash",
            bash->exit_code == 0 ? ava::agent::ToolTimelineStatus::Success : ava::agent::ToolTimelineStatus::Error,
            "exit " + std::to_string(bash->exit_code));
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  result.handled = false;
  return result;
}

}  // namespace

bool is_backend_command(std::string_view line) noexcept { return find_command_catalog_entry(line) != nullptr; }

std::string command_hotkeys_text(const std::vector<CommandHotkey>& hotkeys) {
  const auto items = effective_hotkeys(hotkeys);
  std::size_t action_width = 0;
  std::size_t keys_width = 0;
  for (const auto& item : items) {
    action_width = std::max(action_width, item.action.size());
    keys_width = std::max(keys_width, item.keys.size());
  }

  std::string output = "Hotkeys:\n";
  for (const auto& item : items) {
    output += "  " + item.action;
    if (item.action.size() < action_width) output += std::string(action_width - item.action.size(), ' ');
    output += "  " + item.keys;
    if (item.keys.size() < keys_width) output += std::string(keys_width - item.keys.size(), ' ');
    output += "  " + item.description + '\n';
  }
  return output;
}

std::string command_help_text(const std::vector<CommandHotkey>& hotkeys) {
  std::string output = "Commands:\n";
  output += command_rows(true);
  output += "\nUnavailable commands:\n";
  output += command_rows(false);
  output += '\n';
  output += command_hotkeys_text(hotkeys);
  if (!output.empty() && output.back() == '\n') output.pop_back();
  return output;
}

ava::core::Result<CommandResult> run_command(RuntimeSession& session, CommandRequest request) {
  CommandResult result;
  if (request.command.empty()) return result;

  const auto* entry = find_command_catalog_entry(request.command);
  if (!entry) return result;
  request.command = normalize_command_line(request.command, *entry);

  if (!entry->enabled) {
    result.handled = true;
    add_output(result, entry->command + " is disabled: " + entry->disabled_reason);
    return result;
  }

  if (request.command == "/quit" || request.command == "/exit") {
    result.handled = true;
    result.quit = true;
    return result;
  }
  if (request.command == "/help") {
    result.handled = true;
    add_output(result, command_help_text(request.hotkeys));
    return result;
  }
  if (request.command == "/hotkeys") {
    result.handled = true;
    add_output(result, command_hotkeys_text(request.hotkeys));
    return result;
  }
  if (starts_with_command(request.command, "/connect")) {
    return run_connect_command(session, request);
  }
  if (request.command == "/sessions") {
    result.handled = true;
    auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir);
    if (!sessions) {
      add_output(result, sessions.error().format());
      return result;
    }
    if (sessions->empty()) {
      add_output(result, "No sessions for this workspace.");
      return result;
    }
    std::string output;
    for (const auto& summary : *sessions) {
      output += summary.session_id + "  entries=" + std::to_string(summary.entry_count);
      if (!summary.last_updated.empty()) output += "  updated=" + summary.last_updated;
      output += '\n';
    }
    add_output(result, std::move(output));
    return result;
  }
  if (request.command == "/mode") {
    result.handled = true;
    const auto new_mode = ava::agent::toggle_mode(session.mode);
    auto prompt_state = select_runtime_prompt_state(session, new_mode);
    if (!prompt_state) return std::unexpected(std::move(prompt_state.error()));
    if (auto appended = append_mode_change(session.store, new_mode); !appended) {
      return std::unexpected(std::move(appended.error()));
    }
    apply_runtime_prompt_state(session, std::move(*prompt_state));
    add_output(result, "mode switched to " + ava::agent::to_string(session.mode));
    return result;
  }
  if (request.command == "/context") {
    result.handled = true;
    if (session.context_sources.empty()) {
      add_output(result, "No context sources loaded.");
      return result;
    }
    std::string output;
    for (const auto& source : session.context_sources) {
      output += ava::context::to_string(source.source_type) + "  " + source.path.string() +
                "  bytes=" + std::to_string(source.byte_count) + '\n';
    }
    add_output(result, std::move(output));
    return result;
  }
  if (request.command == "/stats") {
    result.handled = true;
    auto entries = session.store.load();
    if (!entries) {
      add_output(result, entries.error().format());
      return result;
    }
    add_output(result, format_session_stats_text(session, ava::session::compute_session_stats(*entries)));
    return result;
  }
  if (starts_with_command(request.command, "/compact")) {
    result.handled = true;
    auto fail_compaction = [&](ava::core::Error error) -> ava::core::Result<CommandResult> {
      if (request.propagate_compaction_errors) return std::unexpected(std::move(error));
      add_output(result, error.format());
      return result;
    };
    const auto instructions = command_argument(request.command, "/compact");
    if (!request.compaction_summary_generator) {
      return fail_compaction(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "/compact requires provider-backed summary generation"));
    }
    auto config = ava::session::load_compaction_config(session.paths);
    if (!config) {
      return fail_compaction(std::move(config.error()));
    }

    constexpr std::size_t max_compaction_attempts = 2;
    std::size_t last_snapshot_entries = 0;
    std::size_t last_current_entries = 0;
    for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt) {
      ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
          std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
      if (request.session_mutex) {
        std::lock_guard lock(*request.session_mutex);
        entries = session.store.load();
      } else {
        entries = session.store.load();
      }
      if (!entries) {
        return fail_compaction(std::move(entries.error()));
      }
      const auto estimated_tokens = ava::session::estimate_session_tokens(*entries);
      auto summary = request.compaction_summary_generator(*entries, *config, instructions, estimated_tokens);
      if (!summary) {
        return fail_compaction(std::move(summary.error()));
      }
      if (summary->empty()) {
        return fail_compaction(ava::core::Error(ava::core::ErrorCategory::Provider,
                                                "compaction summary generation returned an empty summary"));
      }
      if (summary->size() > config->max_summary_bytes) {
        auto error =
            ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
        error.with_context("max_summary_bytes", std::to_string(config->max_summary_bytes));
        error.with_context("summary_bytes", std::to_string(summary->size()));
        return fail_compaction(std::move(error));
      }

      bool snapshot_stale = false;
      auto validate_and_append = [&]() -> ava::core::VoidResult {
        auto current_entries = session.store.load();
        if (!current_entries) return std::unexpected(std::move(current_entries.error()));
        if (!same_session_snapshot(*entries, *current_entries)) {
          snapshot_stale = true;
          last_snapshot_entries = entries->size();
          last_current_entries = current_entries->size();
          return {};
        }
        return ava::session::append_manual_compaction(
            session.store, ava::session::ManualCompactionRequest{.summary = *summary,
                                                                 .instructions = instructions,
                                                                 .config = *config,
                                                                 .estimated_tokens = estimated_tokens,
                                                                 .threshold_tokens = 0,
                                                                 .trigger = "manual",
                                                                 .recent_context = ""});
      };
      ava::core::VoidResult appended;
      if (request.session_mutex) {
        std::lock_guard lock(*request.session_mutex);
        appended = validate_and_append();
      } else {
        appended = validate_and_append();
      }
      if (!appended) {
        return fail_compaction(std::move(appended.error()));
      }
      if (!snapshot_stale) {
        add_output(result, "compaction summary recorded");
        return result;
      }
    }
    return fail_compaction(stale_compaction_snapshot_error("manual", last_snapshot_entries, last_current_entries));
  }
  if (request.command == "/export") {
    result.handled = true;
    auto entries = session.store.load();
    if (!entries) {
      add_output(result, entries.error().format());
      return result;
    }
    add_output(result, ava::session::format_session_markdown(*entries));
    return result;
  }

  if (entry->hint.empty() && starts_with_command(request.command, entry->command)) {
    result.handled = true;
    add_output(result, missing_argument(entry->command));
    return result;
  }

  if (request.command == "/glob") {
    result.handled = true;
    add_output(result, missing_argument("/glob <pattern>"));
    return result;
  }
  if (request.command == "/grep") {
    result.handled = true;
    add_output(result, missing_argument("/grep <text> [glob]"));
    return result;
  }
  if (request.command == "/read") {
    result.handled = true;
    add_output(result, missing_argument("/read <path>"));
    return result;
  }
  if (request.command == "/write") {
    result.handled = true;
    add_output(result, missing_argument("/write <path> <text>"));
    return result;
  }
  if (request.command == "/bash") {
    result.handled = true;
    add_output(result, missing_argument("/bash <command>"));
    return result;
  }

  return run_tool_command(session, request);
}

}  // namespace ava::app
