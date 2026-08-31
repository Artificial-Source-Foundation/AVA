#include "sys.h"
#include "ava/http/curl_transport.h"
#include "ava/app/commands.h"
#include "ava/app/line_shell.h"
#include "ava/app/line_shell_internal.h"
#include "ava/app/onboarding.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_credentials.h"
#include "ava/tui/composer.h"
#include "ava/tui/session_grants.h"
#include "ava/tui/terminal.h"
#include "ava/config/auth.h"
#include "ava/session/compaction.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/catalog.h"
#include "ava/provider/registry.h"
#include "ava/core/version.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <poll.h>
#include <unistd.h>

namespace ava::app::line_shell_internal {

namespace version = ava::core::version;

namespace {

constexpr std::size_t kMaximumInvalidPromptAnswers = 3;

void write_sanitized_line(std::ostream& output, std::string_view text)
{
  output << ava::tui::sanitize_terminal_text(text) << '\n';
}

void write_sanitized_block(std::ostream& output, std::string_view text)
{
  for (auto const& line : ava::tui::split_lines(text)) write_sanitized_line(output, line);
}

void write_sanitized_field(std::ostream& output, std::string_view label, std::string_view value)
{
  if (value.empty())
    return;
  auto const lines = ava::tui::split_lines(value);
  for (std::size_t index = 0; index < lines.size(); ++index)
  {
    write_sanitized_line(output, (index == 0 ? std::string(label) : std::string(label.size(), ' ')) + lines[index]);
  }
}

std::string_view trim_ascii_space(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

std::string normalized_answer(std::string_view value)
{
  value = trim_ascii_space(value);
  std::string normalized;
  normalized.reserve(value.size());
  for (char const ch : value) normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return normalized;
}

bool is_ascii_digits(std::string_view value)
{
  value = trim_ascii_space(value);
  return !value.empty() && std::ranges::all_of(value, [](char character) { return character >= '0' && character <= '9'; });
}

std::optional<std::size_t> one_based_option_index(std::string_view value, std::size_t option_count)
{
  value = trim_ascii_space(value);
  if (value.empty())
    return std::nullopt;
  std::size_t number = 0;
  auto const [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
  if (error != std::errc{} || end != value.data() + value.size() || number == 0 || number > option_count)
    return std::nullopt;
  return number - 1;
}

ava::core::Error question_canceled_error(std::string message = "question prompt canceled")
{
  return ava::core::Error(ava::core::ErrorCategory::Tool, std::move(message));
}

bool line_question_auto_resolved_while_waiting(ava::agent::QuestionPrompt const& prompt, std::istream& input)
{
  if (!prompt.auto_resolve || &input != &std::cin)
    return false;
  while (!prompt.auto_resolve())
  {
    if (input.rdbuf()->in_avail() > 0)
      return false;
    pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    auto const ready = ::poll(&descriptor, 1, 100);
    if (ready > 0 || (ready < 0 && errno != EINTR))
      return false;
  }
  return true;
}

void print_resume_command(ava::session::SessionStore const& store, std::ostream& output)
{
  if (store.is_ephemeral())
  {
    write_sanitized_line(output, "Session history was not saved (--no-session).");
    return;
  }
  write_sanitized_line(output, "Resume with: ava --session " + store.session_id());
}

}  // namespace

BoundedLineStatus read_bounded_line(std::istream& input, std::string& line, std::size_t maximum_bytes)
{
  line.clear();
  bool consumed = false;
  bool too_long = false;
  char character = '\0';
  while (input.get(character))
  {
    consumed = true;
    if (character == '\n')
      return too_long ? BoundedLineStatus::TooLong : BoundedLineStatus::Line;
    if (too_long)
      continue;
    if (line.size() == maximum_bytes)
    {
      line.clear();
      too_long = true;
      continue;
    }
    line.push_back(character);
  }
  if (!input.eof())
    return BoundedLineStatus::InputError;
  if (too_long)
    return BoundedLineStatus::TooLong;
  return consumed ? BoundedLineStatus::Line : BoundedLineStatus::EndOfInput;
}

LinePermissionChoice resolve_line_permission_prompt(ava::permissions::PermissionPrompt const& prompt, bool allow_session_available,
                                                    bool allow_remember_available, bool deny_remember_available, std::istream& input, std::ostream& output,
                                                    std::string& user_guidance)
{
  user_guidance.clear();
  write_sanitized_line(output, "Permission required");
  write_sanitized_field(output, "Tool: ", prompt.tool_name);
  write_sanitized_field(output, "Operation: ", ava::permissions::to_string(prompt.operation));
  write_sanitized_field(output, "Target: ", prompt.target_path.generic_string());
  write_sanitized_field(output, "Command: ", prompt.command);
  write_sanitized_field(output, "Risk: ", ava::permissions::to_string(prompt.risk));
  write_sanitized_field(output, "Reason: ", prompt.reason);
  if (!prompt.diff_preview.empty())
  {
    write_sanitized_line(output, "Proposed changes:");
    write_sanitized_block(output, prompt.diff_preview);
    if (prompt.diff_truncated)
      write_sanitized_line(output, "(preview truncated)");
  }

  std::vector<std::pair<LinePermissionChoice, std::string>> choices;
  choices.emplace_back(LinePermissionChoice::Deny, "Deny once");
  choices.emplace_back(LinePermissionChoice::Allow, "Allow once");
  if (allow_session_available)
    choices.emplace_back(LinePermissionChoice::AllowSession, "Allow for this session");
  if (allow_remember_available)
    choices.emplace_back(LinePermissionChoice::AllowRemember, "Always allow this workspace scope");
  if (deny_remember_available)
    choices.emplace_back(LinePermissionChoice::DenyRemember, "Always deny this workspace scope");
  for (std::size_t index = 0; index < choices.size(); ++index) write_sanitized_line(output, std::to_string(index + 1) + ") " + choices[index].second);
  std::string text_choices = "You may also type allow, deny";
  if (allow_session_available)
    text_choices += ", session";
  if (allow_remember_available)
    text_choices += ", always allow";
  if (deny_remember_available)
    text_choices += ", always deny";
  write_sanitized_line(output, text_choices + ", guide, or cancel.");

  for (std::size_t attempt = 0; attempt < kMaximumInvalidPromptAnswers; ++attempt)
  {
    output << "Permission choice> " << std::flush;
    std::string answer;
    auto const status = read_bounded_line(input, answer, kLineShellMaxPromptAnswerBytes);
    if (status == BoundedLineStatus::EndOfInput || status == BoundedLineStatus::InputError)
    {
      write_sanitized_line(output, "Permission canceled at end of input; the request was not allowed.");
      return LinePermissionChoice::Cancel;
    }
    if (status == BoundedLineStatus::TooLong)
    {
      write_sanitized_line(output, "Permission answer is too long; enter one listed number or word.");
      continue;
    }

    auto const normalized = normalized_answer(answer);
    if (auto const index = one_based_option_index(answer, choices.size()))
      return choices[*index].first;
    if (normalized == "deny" || normalized == "d" || normalized == "no")
      return LinePermissionChoice::Deny;
    if (normalized == "allow" || normalized == "a" || normalized == "yes")
      return LinePermissionChoice::Allow;
    if (allow_session_available && normalized == "session")
      return LinePermissionChoice::AllowSession;
    if (allow_remember_available && normalized == "always allow")
      return LinePermissionChoice::AllowRemember;
    if (deny_remember_available && (normalized == "always deny" || normalized == "never"))
      return LinePermissionChoice::DenyRemember;
    if (normalized == "cancel" || normalized == "c")
    {
      write_sanitized_line(output, "Permission canceled; the request was not allowed.");
      return LinePermissionChoice::Cancel;
    }
    if (normalized == "guide" || normalized == "g")
    {
      output << "Optional denial guidance for the model> " << std::flush;
      std::string guidance;
      auto const guidance_status = read_bounded_line(input, guidance, ava::permissions::kMaxPermissionUserGuidanceBytes);
      if (guidance_status == BoundedLineStatus::EndOfInput || guidance_status == BoundedLineStatus::InputError)
      {
        write_sanitized_line(output, "Permission canceled at end of input; the request was not allowed.");
        return LinePermissionChoice::Cancel;
      }
      if (guidance_status == BoundedLineStatus::TooLong)
      {
        write_sanitized_line(output, "Denial guidance is too long; the request was denied without guidance.");
        return LinePermissionChoice::Deny;
      }
      if (auto validated = ava::permissions::validated_permission_user_guidance(guidance))
        user_guidance = std::move(*validated);
      else if (!guidance.empty())
        write_sanitized_line(output, "Denial guidance contained unsupported text and was omitted.");
      return LinePermissionChoice::Deny;
    }
    write_sanitized_line(output, "Invalid permission answer; enter one listed number or word.");
  }
  write_sanitized_line(output, "Too many invalid permission answers; the request was canceled and not allowed.");
  return LinePermissionChoice::Cancel;
}

ava::core::Result<ava::agent::QuestionAnswer> resolve_line_question_prompt(ava::agent::QuestionPrompt const& prompt, std::istream& input, std::ostream& output)
{
  if (prompt.auto_resolve && prompt.auto_resolve())
    return ava::agent::QuestionAnswer{.selected_options = {"done"}, .custom_text = {}};
  if (prompt.secret)
  {
    write_sanitized_line(output, "Secret input is not accepted in --line-shell. Exit and run ava login <provider> --api-key in a private terminal.");
    return std::unexpected(question_canceled_error("secret question prompt is unavailable in --line-shell"));
  }

  write_sanitized_line(output, prompt.header.empty() ? "Question" : "Question: " + prompt.header);
  write_sanitized_block(output, prompt.question);
  for (std::size_t index = 0; index < prompt.options.size(); ++index)
    write_sanitized_line(output, std::to_string(index + 1) + ") " + prompt.options[index].label);
  if (prompt.multiple)
    write_sanitized_line(output, "Enter comma-separated option numbers, custom text, none, or cancel.");
  else if (prompt.allow_custom)
    write_sanitized_line(output, "Enter one option number, custom text, or cancel.");
  else
    write_sanitized_line(output, "Enter one option number or cancel.");

  for (std::size_t attempt = 0; attempt < kMaximumInvalidPromptAnswers; ++attempt)
  {
    output << "Answer> " << std::flush;
    if (line_question_auto_resolved_while_waiting(prompt, input))
    {
      write_sanitized_line(output, "Question resolved automatically.");
      return ava::agent::QuestionAnswer{.selected_options = {"done"}, .custom_text = {}};
    }
    std::string answer_text;
    auto const status = read_bounded_line(input, answer_text, kLineShellMaxPromptAnswerBytes);
    if (status == BoundedLineStatus::EndOfInput || status == BoundedLineStatus::InputError)
    {
      write_sanitized_line(output, "Question canceled at end of input.");
      return std::unexpected(question_canceled_error());
    }
    if (status == BoundedLineStatus::TooLong)
    {
      write_sanitized_line(output, "Question answer is too long; enter a shorter answer.");
      continue;
    }

    auto const normalized = normalized_answer(answer_text);
    if (normalized == "cancel" || normalized == "c")
    {
      write_sanitized_line(output, "Question canceled.");
      return std::unexpected(question_canceled_error());
    }
    if (prompt.multiple && (normalized.empty() || normalized == "none"))
      return ava::agent::QuestionAnswer{};

    if (!prompt.multiple)
    {
      if (auto const selected = one_based_option_index(answer_text, prompt.options.size()))
        return ava::agent::QuestionAnswer{.selected_options = {prompt.options[*selected].value}, .custom_text = {}};
      if (prompt.allow_custom && !trim_ascii_space(answer_text).empty() && !is_ascii_digits(answer_text))
        return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = std::move(answer_text)};
      write_sanitized_line(output, "Invalid question answer; enter one listed number or permitted custom text.");
      continue;
    }

    ava::agent::QuestionAnswer answer;
    bool numeric_list = true;
    std::size_t start = 0;
    while (start <= answer_text.size())
    {
      auto const comma = answer_text.find(',', start);
      auto const token = std::string_view(answer_text).substr(start, comma == std::string::npos ? answer_text.size() - start : comma - start);
      auto const selected = one_based_option_index(token, prompt.options.size());
      if (!selected || std::ranges::find(answer.selected_options, prompt.options[*selected].value) != answer.selected_options.end())
      {
        numeric_list = false;
        break;
      }
      answer.selected_options.push_back(prompt.options[*selected].value);
      if (comma == std::string::npos)
        break;
      start = comma + 1;
    }
    if (numeric_list && !answer.selected_options.empty())
      return answer;
    if (prompt.allow_custom && answer_text.find(',') == std::string::npos && !trim_ascii_space(answer_text).empty() && !is_ascii_digits(answer_text))
      return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = std::move(answer_text)};
    write_sanitized_line(output, "Invalid question answer; use unique comma-separated numbers or permitted custom text.");
  }
  write_sanitized_line(output, "Too many invalid question answers; the question was canceled.");
  return std::unexpected(question_canceled_error("question prompt canceled after too many invalid answers"));
}

bool is_compact_command(std::string_view line) noexcept
{
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
}

bool is_display_settings_command(std::string_view line) noexcept
{
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
  return line == "/theme" || (line.starts_with("/theme") && line.size() > 6 && line[6] == ' ') || line == "/images" ||
         (line.starts_with("/images") && line.size() > 7 && line[7] == ' ') || line == "/image-width" ||
         (line.starts_with("/image-width") && line.size() > 12 && line[12] == ' ') || line == "/cursor" ||
         (line.starts_with("/cursor") && line.size() > 7 && line[7] == ' ') || line == "/reload theme" || line == "/reload themes" || line == "/reload display";
}

void add_output(LineResult& result, std::string text)
{
  result.output.push_back(std::move(text));
}

template <typename Callback>
LineResult with_provider_runtime(ShellState& state, std::string_view offline_suffix, Callback callback, std::string_view provider_override = {})
{
  runtime::session_ts& unlocked_session = state.unlocked_session;
  auto const session_snapshot = [&] {
    SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
    return std::tuple{session_r->is_offline(), session_r->model().provider_id, session_r->session_process_scope()};
  }();
  auto const& [offline, model_provider_id, session_process_scope] = session_snapshot;

  LineResult line_result;
  if (offline)
  {
    add_output(line_result, ava::app::offline_provider_error("prompt").format() + std::string(offline_suffix));
    return line_result;
  }
  auto const provider_id = provider_override.empty() ? std::string_view(model_provider_id) : provider_override;
  if (!session_process_scope)
  {
    add_output(line_result, "prompt process authority is unavailable" + std::string(offline_suffix));
    return line_result;
  }
  ava::http::CurlCliTransport transport(*session_process_scope);
  CRITICAL_AREA_BEGIN_R(session);
  auto ensured_provider_catalog = session_r->ensure_provider_catalog();
  ava::app::runtime::RunOptions run_options;
  run_options.enable_transport_retries = true;
  auto prepared = ava::app::prepare_runtime_credentials(session_r->paths(), provider_id, std::move(run_options), transport, "prompt", ensured_provider_catalog);
  CRITICAL_AREA_END_R(session);
  if (!prepared)
  {
    if (provider_override.empty() && prepared.error().message().find("requires auth for provider") != std::string::npos)
      add_output(line_result, ava::app::provider_auth_required_message(unlocked_session, offline_suffix));
    else
      add_output(line_result, prepared.error().format() + std::string(offline_suffix));
    return line_result;
  }
  auto provider = ensured_provider_catalog->create(provider_id);
  if (!provider)
  {
    add_output(line_result, provider.error().format() + std::string(offline_suffix));
    return line_result;
  }
  return callback(**provider, transport, std::move(*prepared));
}

LineResult handle_line(ShellState& state, std::string const& line, ava::permissions::PermissionResolver permission_resolver,
                       ava::agent::QuestionResolver question_resolver, std::vector<ava::app::CommandHotkey> const& hotkeys,
                       ava::event::RuntimeEventSink event_sink, std::function<bool()> cancel_requested,
                       std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages,
                       std::vector<ava::session::ImageAttachmentRef> image_attachments, std::string request_id,
                       ava::agent::SubagentLaunchSink on_subagent_launch, std::shared_ptr<PluginUiInvocationCapability> plugin_ui_capability)
{
  runtime::session_ts& unlocked_session = state.unlocked_session;

  LineResult line_result;
  if (line.empty())
    return line_result;
  if (ava::app::is_backend_command_1(line, unlocked_session))
  {
    if (is_compact_command(line))
    {
      auto const paths = runtime::session_ts::rat(unlocked_session)->paths();
      auto loaded_config = ava::session::load_compaction_config(paths);
      if (!loaded_config)
      {
        add_output(line_result, loaded_config.error().format());
        return line_result;
      }
      auto config = ava::app::resolve_compaction_config(unlocked_session, std::move(*loaded_config));
      if (!config)
      {
        add_output(line_result, config.error().format());
        return line_result;
      }
      auto const summary_provider_id = config->provider_id;
      return with_provider_runtime(
          state, "\nother slash tool commands still work offline.",
          [&](ava::provider::Provider const& provider, ava::http::Transport& transport, ava::app::runtime::RunOptions run_options) {
            run_options.cancel_requested = cancel_requested;
            run_options.event_sink = event_sink;
            if (!request_id.empty())
              run_options.request_id = request_id;
            run_options.on_subagent_launch = on_subagent_launch;
            auto command_result = ava::app::run_command(
                unlocked_session,
                ava::app::CommandRequest{.command = line,
                                         .event_sink = event_sink,
                                         .permission_resolver = permission_resolver,
                                         .question_resolver = question_resolver,
                                         .compaction_summary_generator =
                                             [&](std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                 std::string_view instructions, std::size_t estimated_tokens) {
                                               return ava::app::generate_compaction_summary(unlocked_session, entries, config, instructions, estimated_tokens,
                                                                                            provider, transport, run_options);
                                             },
                                         .cancel_requested = cancel_requested,
                                         .hotkeys = hotkeys});
            if (!command_result)
            {
              LineResult compact_result;
              add_output(compact_result, command_result.error().format());
              return compact_result;
            }
            return LineResult{.quit = command_result->quit,
                              .session_tree_changed = command_result->session_tree_changed,
                              .output = std::move(command_result->output),
                              .tool_timeline = std::move(command_result->tool_timeline)};
          },
          summary_provider_id);
    }
    auto command_result = ava::app::run_command(unlocked_session, ava::app::CommandRequest{.command = line,
                                                                                           .permission_resolver = permission_resolver,
                                                                                           .question_resolver = question_resolver,
                                                                                           .cancel_requested = cancel_requested,
                                                                                           .plugin_ui_capability = std::move(plugin_ui_capability),
                                                                                           .hotkeys = hotkeys});
    if (!command_result)
    {
      add_output(line_result, command_result.error().format());
      return line_result;
    }
    line_result.quit = command_result->quit;
    line_result.session_tree_changed = command_result->session_tree_changed;
    line_result.output = std::move(command_result->output);
    line_result.tool_timeline = std::move(command_result->tool_timeline);
    if (command_result->prompt_message)
    {
      return with_provider_runtime(state, "\nthis command expands to a prompt and needs provider auth.",
                                   [&](ava::provider::Provider const& provider, ava::http::Transport& transport, ava::app::runtime::RunOptions run_options) {
                                     run_options.permission_resolver = permission_resolver;
                                     run_options.question_resolver = question_resolver;
                                     run_options.event_sink = std::move(event_sink);
                                     run_options.cancel_requested = std::move(cancel_requested);
                                     run_options.take_steering_messages = std::move(take_steering_messages);
                                     if (!request_id.empty())
                                       run_options.request_id = request_id;
                                     run_options.on_subagent_launch = on_subagent_launch;
                                     auto result = ava::app::run_prompt(unlocked_session, *command_result->prompt_message, provider, transport, run_options);
                                     LineResult prompt_result;
                                     if (!result)
                                     {
                                       add_output(prompt_result, result.error().format());
                                       return prompt_result;
                                     }
                                     prompt_result.ordinary_turn_committed = true;
                                     prompt_result.tool_timeline = std::move(result->tool_timeline);
                                     if (!result->final_text.empty())
                                     {
                                       add_output(prompt_result, result->final_text);
                                     }
                                     else
                                     {
                                       add_output(prompt_result, "done");
                                     }
                                     return prompt_result;
                                   });
    }
    return line_result;
  }
  if (line.starts_with('/'))
  {
    auto const end = line.find_first_of(" \t\r\n");
    auto const command = line.substr(0, end == std::string::npos ? line.size() : end);
    add_output(line_result, "Unknown command: " + command + ". Type /help to list commands.");
    return line_result;
  }

  return with_provider_runtime(state, "\nslash tool commands still work offline.",
                               [&](ava::provider::Provider const& provider, ava::http::Transport& transport, ava::app::runtime::RunOptions run_options) {
                                 run_options.permission_resolver = permission_resolver;
                                 run_options.question_resolver = question_resolver;
                                 run_options.event_sink = std::move(event_sink);
                                 run_options.cancel_requested = std::move(cancel_requested);
                                 run_options.take_steering_messages = std::move(take_steering_messages);
                                 run_options.image_attachments = std::move(image_attachments);
                                 if (!request_id.empty())
                                   run_options.request_id = std::move(request_id);
                                 run_options.on_subagent_launch = std::move(on_subagent_launch);
                                 auto result = ava::app::run_prompt(unlocked_session, line, provider, transport, run_options);
                                 LineResult prompt_result;
                                 if (!result)
                                 {
                                   add_output(prompt_result, result.error().format());
                                   return prompt_result;
                                 }
                                 prompt_result.ordinary_turn_committed = true;
                                 prompt_result.tool_timeline = std::move(result->tool_timeline);
                                 if (!result->final_text.empty())
                                 {
                                   add_output(prompt_result, result->final_text);
                                 }
                                 else
                                 {
                                   add_output(prompt_result, "done");
                                 }
                                 return prompt_result;
                               });
}

int run_line_shell(ShellState state, std::istream& input, std::ostream& output)
{
  runtime::session_ts& unlocked_session = state.unlocked_session;
  auto const session_summary = [&] {
    SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
    return std::tuple{session_r->mode(), session_r->model().provider_id, session_r->model().model_id};
  }();
  auto const& [initial_mode, provider_id, model_id] = session_summary;

  write_sanitized_line(output, "AVA " + std::string(version::kDisplayVersion) + " line shell");
  write_sanitized_line(output, "Mode: " + ava::agent::to_string(initial_mode) + " | model: " + provider_id + "/" + model_id);
  write_sanitized_line(output, "Type a message or /help. Ctrl-D exits.");

  auto print_current_resume_command = [&] {
    SCOPED_CRITICAL_AREA_R(session_r, unlocked_session);
    print_resume_command(session_r->store, output);
  };

  ava::tui::TuiSessionGrantRegistry session_grants;
  std::string line;
  while (true)
  {
    auto const current_mode = runtime::session_ts::rat(unlocked_session)->mode();
    output << "\n[" << ava::tui::sanitize_terminal_text(ava::agent::to_string(current_mode)) << "] ava> " << std::flush;
    auto const input_status = read_bounded_line(input, line, kLineShellMaxSubmittedBytes);
    if (input_status == BoundedLineStatus::EndOfInput)
    {
      output << '\n';
      print_current_resume_command();
      return 0;
    }
    if (input_status == BoundedLineStatus::InputError)
    {
      write_sanitized_line(output, "Input failed; exiting the line shell.");
      print_current_resume_command();
      return 1;
    }
    if (input_status == BoundedLineStatus::TooLong)
    {
      write_sanitized_line(output, "Input exceeds the 65536-byte line limit; the submitted line was cleared. Enter a shorter line.");
      continue;
    }

    auto const session_id_before = runtime::session_ts::rat(unlocked_session)->store.session_id();
    auto fallback_permission_resolver =
        [&](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
      auto const current_session_id = runtime::session_ts::rat(unlocked_session)->store.session_id();
      bool const allow_session_available = ava::tui::tui_session_grant_eligible(prompt);
      if (allow_session_available && session_grants.matches(current_session_id, prompt))
      {
        ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::AllowSessionGrant, "reused line-shell session grant"};
        decision.resolution_source = "line_shell_session_grant";
        return decision;
      }

      auto const remember_availability = ava::tui::permission_prompt_remember_availability(prompt, true);
      std::string user_guidance;
      auto const choice = resolve_line_permission_prompt(prompt, allow_session_available, remember_availability.allow_remember_available,
                                                         remember_availability.deny_remember_available, input, output, user_guidance);
      if (choice == LinePermissionChoice::Cancel)
      {
        ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Cancel, "line-shell permission request canceled"};
        decision.resolution_source = "line_shell_cancel";
        return decision;
      }
      if (choice == LinePermissionChoice::AllowSession)
      {
        auto const inserted = session_grants.add(current_session_id, prompt);
        if (inserted != ava::tui::TuiSessionGrantInsertResult::Added && inserted != ava::tui::TuiSessionGrantInsertResult::AlreadyPresent)
        {
          write_sanitized_line(output, "The requested session scope is unavailable; permission was denied.");
          ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny, "line-shell session grant unavailable"};
          decision.resolution_source = "line_shell_session_grant_unavailable";
          return decision;
        }
        ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::AllowSessionGrant};
        decision.resolution_source = "line_shell_session_grant";
        return decision;
      }
      bool const remember = choice == LinePermissionChoice::AllowRemember || choice == LinePermissionChoice::DenyRemember;
      bool const allow = choice == LinePermissionChoice::Allow || choice == LinePermissionChoice::AllowRemember;
      if (remember)
      {
        auto remembered = remember_permission_rule_for_prompt(
            unlocked_session, prompt, allow ? ava::permissions::PermissionAction::Allow : ava::permissions::PermissionAction::Deny, "line_shell_prompt");
        if (!remembered)
        {
          write_sanitized_block(output, "Permission rule could not be saved; the request was denied: " + remembered.error().format());
          ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny,
                                                                  "line-shell permission rule persistence failed"};
          decision.resolution_source = "line_shell_remember_failed";
          return decision;
        }
        ava::permissions::PermissionResolutionDecision decision{
            allow ? ava::permissions::PermissionResolution::Allow : ava::permissions::PermissionResolution::Deny,
            allow ? "remembered allow rule" : "remembered deny rule"};
        decision.resolution_source = "persistent_rule_added";
        decision.rule_id = remembered->rule_id;
        return decision;
      }
      ava::permissions::PermissionResolutionDecision decision{allow ? ava::permissions::PermissionResolution::Allow
                                                                    : ava::permissions::PermissionResolution::Deny};
      if (!allow)
        decision.user_guidance = std::move(user_guidance);
      decision.resolution_source = allow ? "line_shell_allow_once" : "line_shell_deny_once";
      return decision;
    };
    auto const permission_store = runtime::session_ts::rat(unlocked_session)->permission_rule_store();
    auto permission_resolver = ava::permissions::build_persistent_permission_rule_resolver(permission_store, std::move(fallback_permission_resolver));
    auto question_resolver = [&](ava::agent::QuestionPrompt const& prompt) { return resolve_line_question_prompt(prompt, input, output); };
    auto const result = handle_line(state, line, std::move(permission_resolver), std::move(question_resolver));
    for (auto const& result_output : result.output) write_sanitized_block(output, result_output);

    auto const current_session_id = runtime::session_ts::rat(unlocked_session)->store.session_id();
    static_cast<void>(session_grants.clear_for_session_transition(session_id_before, current_session_id));
    if (result.quit)
    {
      print_current_resume_command();
      return 0;
    }
  }
}
}  // namespace ava::app::line_shell_internal

namespace ava::app {

int run_interactive(runtime::session_ts& unlocked_session, bool force_line_shell)
{
  AVA_ASSERT_SESSION_UNLOCKED(unlocked_session, "calling run_interactive");

  line_shell_internal::ShellState state{.unlocked_session = unlocked_session};
  if (!force_line_shell && ava::tui::terminal_is_tty())
    return line_shell_internal::run_tui(state);
  return line_shell_internal::run_line_shell(state, std::cin, std::cout);
}

}  // namespace ava::app
