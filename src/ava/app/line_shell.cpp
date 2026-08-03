#include "sys.h"
#include "ava/http/curl_transport.h"
#include "ava/app/commands.h"
#include "ava/app/line_shell.h"
#include "ava/app/line_shell_internal.h"
#include "ava/app/onboarding.h"
#include "ava/app/runtime.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "ava/config/auth.h"
#include "ava/session/compaction.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/catalog.h"
#include "ava/app/runtime_credentials.h"
#include "ava/provider/registry.h"
#include "ava/core/version.h"

#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app::line_shell_internal {

namespace version = ava::core::version;

void print_shell_help()
{
  std::cout << ava::app::command_help_text() << '\n';
}

void print_resume_command(ava::session::SessionStore const& store)
{
  if (store.is_ephemeral())
  {
    std::cout << "Session history was not saved (--no-session)\n";
    return;
  }
  std::cout << "Resume this session with: ava --session " << store.session_id() << '\n';
}

bool is_compact_command(std::string_view line) noexcept
{
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
}

bool is_display_settings_command(std::string_view line) noexcept
{
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
  return line == "/theme" || (line.starts_with("/theme") && line.size() > 6 && line[6] == ' ') || line == "/reload theme" || line == "/reload themes" ||
         line == "/reload display";
}

void add_output(LineResult& result, std::string text)
{
  result.output.push_back(std::move(text));
}

template <typename Callback>
LineResult with_provider_runtime(ShellState& state, std::string_view offline_suffix, Callback callback, std::string_view provider_override = {})
{
  // MT: is this really single-threaded? `state_session.is_offline()` requires a lock,
  // but obiously could be offline immediately after returning from that accessor if this isn't single threaded.
  // For now just keep the lock for the whole duration of the function, but it smell badly.
  runtime::Session const& state_session = *runtime::session_ts::rat(state.unlocked_session);

  LineResult line_result;
  if (state_session.is_offline())
  {
    add_output(line_result, ava::app::offline_provider_error("prompt").format() + std::string(offline_suffix));
    return line_result;
  }
  auto const provider_id = provider_override.empty() ? std::string_view(state_session.model().provider_id) : provider_override;
  ava::http::CurlCliTransport transport;
  auto catalog = state_session.provider_catalog() ? state_session.provider_catalog() : ava::provider::ProviderCatalog::build_builtins_only();
  ava::app::runtime::RunOptions run_options;
  run_options.enable_transport_retries = true;
  auto prepared = ava::app::prepare_runtime_credentials(state_session.paths(), provider_id, std::move(run_options), transport, "prompt", catalog);
  if (!prepared)
  {
    if (provider_override.empty() && prepared.error().message().find("requires auth for provider") != std::string::npos)
      add_output(line_result, ava::app::provider_auth_required_message(state_session, offline_suffix));
    else
      add_output(line_result, prepared.error().format() + std::string(offline_suffix));
    return line_result;
  }
  auto provider = catalog->create(provider_id);
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
                       ava::agent::SubagentLaunchSink on_subagent_launch)
{
  // MT: is this really single-threaded?
  runtime::Session& state_session = *runtime::session_ts::wat(state.unlocked_session);

  LineResult line_result;
  if (line.empty())
    return line_result;
  if (ava::app::is_backend_command_1(line, state_session))
  {
    if (is_compact_command(line))
    {
      auto loaded_config = ava::session::load_compaction_config(state_session.paths());
      if (!loaded_config)
      {
        add_output(line_result, loaded_config.error().format());
        return line_result;
      }
      auto config = ava::app::resolve_compaction_config(state_session, std::move(*loaded_config));
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
                state_session,
                ava::app::CommandRequest{.command = line,
                                         .event_sink = event_sink,
                                         .permission_resolver = permission_resolver,
                                         .question_resolver = question_resolver,
                                         .compaction_summary_generator =
                                             [&](std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                 std::string_view instructions, std::size_t estimated_tokens) {
                                               return ava::app::generate_compaction_summary(state_session, entries, config, instructions, estimated_tokens,
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
    auto command_result = ava::app::run_command(state_session, ava::app::CommandRequest{.command = line,
                                                                                        .permission_resolver = permission_resolver,
                                                                                        .question_resolver = question_resolver,
                                                                                        .cancel_requested = cancel_requested,
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
                                     auto result = ava::app::run_prompt(state_session, *command_result->prompt_message, provider, transport, run_options);
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
                                 auto result = ava::app::run_prompt(state_session, line, provider, transport, run_options);
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

int run_line_shell(ShellState state)
{
  // MT: is this really single-threaded?
  runtime::Session const& state_session = *runtime::session_ts::rat(state.unlocked_session);

  std::cout << "AVA " << version::kDisplayVersion << " terminal shell\n";
  std::cout << "mode: " << ava::agent::to_string(state_session.mode()) << " | session: " << state_session.store.session_id() << "\n";
  std::cout << "provider: " << state_session.model().provider_id << " | model: " << state_session.model().model_id << "\n";
  print_shell_help();

  std::string line;
  while (true)
  {
    std::cout << "\n[" << ava::agent::to_string(state_session.mode()) << "] ava> " << std::flush;
    if (!std::getline(std::cin, line))
    {
      std::cout << '\n';
      print_resume_command(state_session.store);
      return 0;
    }

    auto permission_resolver = ava::permissions::build_persistent_permission_rule_resolver(state_session.permission_rule_store(), nullptr);
    auto const result = handle_line(state, line, permission_resolver);
    for (auto const& output : result.output)
    {
      for (auto const& output_line : ava::tui::split_lines(output))
      {
        std::cout << ava::tui::sanitize_terminal_text(output_line) << '\n';
      }
    }
    if (result.quit)
    {
      print_resume_command(state_session.store);
      return 0;
    }
  }
}

}  // namespace ava::app::line_shell_internal

namespace ava::app {

int run_interactive(runtime::session_ts& unlocked_session)
{
  line_shell_internal::ShellState state{.unlocked_session = unlocked_session};
  if (ava::tui::terminal_is_tty())
    return line_shell_internal::run_tui(state);
  return line_shell_internal::run_line_shell(state);
}

}  // namespace ava::app
