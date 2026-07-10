#include "sys.h"
#include "ava/app/rpc_mode.h"

#include "ava/app/command_registry.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/output.h"
#include "ava/app/rpc/prompt_worker.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc/serialization_json.h"
#include "ava/app/rpc/session_commands.h"

#include "ava/provider/curl_transport.h"
#include "ava/provider/provider_utils.h"
#include "ava/provider/registry.h"

#include "ava/session/attachments.h"

#include <atomic>
#include <functional>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "debug.h"
#ifdef CWDEBUG
#include "ava/debug/print_reference.h"
#endif

namespace ava::app {
namespace {

std::filesystem::path resolve_rpc_attachment_path(std::filesystem::path const& current_dir,
                                                  std::string const& attachment_path)
{
  auto path = std::filesystem::path(attachment_path);
  if (path.is_absolute())
    return path;
  return current_dir / path;
}

int rpc_base64_value(char ch)
{
  if (ch >= 'A' && ch <= 'Z')
    return ch - 'A';
  if (ch >= 'a' && ch <= 'z')
    return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9')
    return ch - '0' + 52;
  if (ch == '+')
    return 62;
  if (ch == '/')
    return 63;
  return -1;
}

ava::core::Result<std::string> decode_rpc_image_base64(std::string_view data)
{
  if (!ava::provider::is_valid_base64(data)) {
    return std::unexpected(rpc::invalid_rpc("RPC image upload data must be base64"));
  }
  std::size_t padding = 0;
  if (!data.empty() && data.back() == '=') {
    ++padding;
    if (data.size() >= 2 && data[data.size() - 2] == '=') ++padding;
  }
  auto const decoded_size = (data.size() / 4U) * 3U - padding;
  if (decoded_size == 0 || decoded_size > ava::session::kMaxImageAttachmentBytes) {
    auto error = rpc::invalid_rpc("RPC image upload byte size is invalid");
    error.with_context("max_bytes", std::to_string(ava::session::kMaxImageAttachmentBytes));
    error.with_context("byte_size", std::to_string(decoded_size));
    return std::unexpected(std::move(error));
  }

  std::string bytes;
  bytes.reserve(decoded_size);
  for (std::size_t index = 0; index < data.size(); index += 4U) {
    auto const a = rpc_base64_value(data[index]);
    auto const b = rpc_base64_value(data[index + 1]);
    auto const c = data[index + 2] == '=' ? 0 : rpc_base64_value(data[index + 2]);
    auto const d = data[index + 3] == '=' ? 0 : rpc_base64_value(data[index + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) {
      return std::unexpected(rpc::invalid_rpc("RPC image upload data must be base64"));
    }
    auto const value = static_cast<unsigned int>((a << 18) | (b << 12) | (c << 6) | d);
    bytes.push_back(static_cast<char>((value >> 16) & 0xFFU));
    if (data[index + 2] != '=') bytes.push_back(static_cast<char>((value >> 8) & 0xFFU));
    if (data[index + 3] != '=') bytes.push_back(static_cast<char>(value & 0xFFU));
  }
  return bytes;
}

}  // namespace

ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options,
                                   ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                   ava::provider::Transport& auth_transport, RuntimeRunOptions runtime_options,
                                   std::istream& in, std::ostream& out)
{
  // This function is called first-thing after creating a thread.
  Debug(NAMESPACE_DEBUG::init_thread("run_rpc_loop"));

  DoutEntering(dc::rpc, "run_rpc_loop(" << session << ", " << open_options << ", " << provider << ", " << auth_transport << ", " << runtime_options << ", " << in << ", " << out << ")");

  rpc::RpcOutput output(out);
  rpc::RpcRunState run_state;
  rpc::PendingResolverState pending_state;
  std::mutex session_mutex;
  std::optional<std::jthread> prompt_worker;
  if (!runtime_options.permission_resolver) {
    runtime_options.permission_resolver = build_headless_permission_resolver(HeadlessPermissionPolicyOptions{});
  }
  runtime_options.permission_resolver = ava::permissions::build_persistent_permission_rule_resolver(
      rpc::permission_rule_store_for_session(session), std::move(runtime_options.permission_resolver));
  std::string const injected_provider_id = session.model.provider_id;

  auto reap_finished_prompt = [&] {
    if (prompt_worker && !rpc::active_run(run_state)) {
      prompt_worker.reset();
    }
  };

  output.on_write_failure = [&] {
    static_cast<void>(rpc::close_input_and_cancel(run_state));
    static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
  };

  std::string line;
  std::optional<ava::core::Error> input_read_error;
  while (true) {
    auto read_line = rpc::read_rpc_line_bounded(in, line);
    if (!read_line) {
      if (read_line.error().category() == ava::core::ErrorCategory::InvalidArgument) {
        if (auto written = rpc::write_error(output, "", read_line.error()); !written) return written;
        continue;
      }
      input_read_error = std::move(read_line.error());
      break;
    }
    if (!*read_line) break;
    reap_finished_prompt();
    if (auto async_error = rpc::take_async_error(run_state)) return std::unexpected(std::move(*async_error));
    auto command = parse_rpc_command_line(line);
    if (!command) {
      if (auto written = rpc::write_error(output, rpc::parse_error_response_id(line), command.error()); !written) {
        return written;
      }
      continue;
    }
    if (auto valid_version = rpc::validate_protocol_version(*command); !valid_version) {
      if (auto written = rpc::write_error(output, command->id, valid_version.error()); !written) return written;
      continue;
    }

    if (command->type == "get_protocol") {
      if (auto written = rpc::write_success(output, command->id, rpc::rpc_protocol_result_json()); !written) {
        return written;
      }
      continue;
    }

    auto session_command =
        rpc::handle_session_rpc_command(rpc::RpcSessionCommandContext{.command = *command,
                                                                      .session = session,
                                                                      .open_options = open_options,
                                                                      .output = output,
                                                                      .run_state = run_state,
                                                                      .session_mutex = session_mutex});
    if (!session_command) return std::unexpected(std::move(session_command.error()));
    if (*session_command) continue;

    if (command->type == "permission_grants") {
      if (auto written =
              rpc::write_success(output, command->id, rpc::permission_session_grants_result_json(pending_state));
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "permission_grant_revoke") {
      if (!command->grant_id || command->grant_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("permission_grant_revoke requires grant_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto revoked = rpc::permission_session_grant_revoke_result_json(pending_state, *command->grant_id);
      if (!revoked) {
        if (auto written = rpc::write_error(output, command->id, revoked.error()); !written) return written;
        continue;
      }
      auto envelope = rpc::resolver_event_envelope("permission_grant_revoked", command->id, command->id,
                                                   rpc::session_id_snapshot(session, session_mutex), *revoked);
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, *revoked); !written) return written;
      continue;
    }

    if (command->type == "permission_grants_clear") {
      auto const cleared = rpc::permission_session_grants_clear_result_json(pending_state);
      auto envelope = rpc::resolver_event_envelope("permission_grants_cleared", command->id, command->id,
                                                   rpc::session_id_snapshot(session, session_mutex), cleared);
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, cleared); !written) return written;
      continue;
    }

    if (command->type == "list_commands") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      std::lock_guard lock(session_mutex);
      auto registry = load_command_registry(
          session, CommandRegistryOptions{
                       .include_builtins = true,
                       .include_prompt_commands = true,
                       .include_skills = true,
                       .include_plugin_commands = true,
                       .include_mcp_prompts = true,
                       .permission_resolver = runtime_options.permission_resolver,
                       .cancel_requested = [&] { return run_state.cancel_requested.load(std::memory_order_relaxed); }});
      if (auto written = rpc::write_success(output, command->id, rpc::command_registry_result_json(registry));
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "invoke_command") {
      if (!command->name || command->name->empty()) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("invoke_command requires name"));
            !written) {
          return written;
        }
        continue;
      }
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }

      std::string slash_command = command->name->starts_with('/') ? *command->name : "/" + *command->name;
      if (command->command_arguments && !command->command_arguments->empty()) {
        slash_command += " " + *command->command_arguments;
      }

      EventBus event_bus;
      rpc::subscribe_event_envelope_writer(event_bus, output);
      std::optional<std::string> prompt_message;
      ava::config::XdgPaths paths;
      {
        std::lock_guard lock(session_mutex);
        paths = session.paths;
        auto result = run_command(session, CommandRequest{.command = std::move(slash_command),
                                                          .event_sink = make_runtime_event_bus_adapter(
                                                              event_bus, rpc::rpc_event_context(command->id)),
                                                          .permission_resolver = runtime_options.permission_resolver,
                                                          .session_mutex = &session_mutex});
        if (!result) {
          if (auto written = rpc::write_error(output, command->id, result.error()); !written) return written;
          continue;
        }
        if (!result->handled) {
          auto error = rpc::invalid_rpc("command not found");
          error.with_context("name", *command->name);
          if (auto written = rpc::write_error(output, command->id, error); !written) return written;
          continue;
        }
        if (result->prompt_message) {
          prompt_message = *result->prompt_message;
        } else {
          if (auto written = rpc::write_success(output, command->id, rpc::command_result_json(*result)); !written) {
            return written;
          }
          continue;
        }
      }

      reap_finished_prompt();
      rpc::set_active_run(run_state, true, command->id);
      prompt_worker.emplace(rpc::make_rpc_prompt_worker(rpc::RpcPromptWorkerOptions{
          .session = session,
          .session_mutex = session_mutex,
          .output = output,
          .run_state = run_state,
          .pending_state = pending_state,
          .injected_provider = provider,
          .injected_provider_id = injected_provider_id,
          .transport = transport,
          .auth_transport = auth_transport,
          .runtime_options = runtime_options,
          .paths = std::move(paths),
          .request_id = command->id,
          .message = std::move(*prompt_message),
      }));
      continue;
    }

    if (command->type == "permission_reply") {
      if (!command->request_id || command->request_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc(command->type + " requires request_id"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->decision) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("permission_reply requires decision"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->correlation_id || command->correlation_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("permission_reply requires correlation_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto resolved = rpc::resolve_permission_reply(pending_state, *command->request_id, *command->correlation_id,
                                                    *command->decision, command->reason);
      if (!resolved) {
        if (auto written = rpc::write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      auto envelope = rpc::resolver_event_envelope(
          "permission_replied", *command->correlation_id, *command->correlation_id,
          rpc::session_id_snapshot(session, session_mutex),
          rpc::permission_reply_payload_json(*command->request_id, *command->decision, command->reason));
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    if (command->type == "question_reply") {
      if (!command->request_id || command->request_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc(command->type + " requires request_id"));
            !written) {
          return written;
        }
        continue;
      }
      if (!command->correlation_id || command->correlation_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("question_reply requires correlation_id"));
            !written) {
          return written;
        }
        continue;
      }
      auto resolved = rpc::resolve_question_reply(pending_state, *command->request_id, *command->correlation_id,
                                                  command->answer, command->selected, command->selected_options);
      if (!resolved) {
        if (auto written = rpc::write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      auto envelope =
          rpc::resolver_event_envelope("question_replied", *command->correlation_id, *command->correlation_id,
                                       rpc::session_id_snapshot(session, session_mutex),
                                       rpc::question_reply_payload_json(*command->request_id, command->answer,
                                                                        command->selected, command->selected_options));
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    if (command->type == "steer") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("steer requires message"));
            !written) {
          return written;
        }
        continue;
      }
      auto queued =
          rpc::queue_rpc_message(run_state.steering_messages, run_state, command->type, command->id, *command->message);
      if (!queued) {
        if (auto written = rpc::write_error(output, command->id, queued.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_queue_event(output, session, session_mutex, "steer_queued", *queued); !written) {
        return written;
      }
      std::string json = "{";
      json += rpc::bool_field_json("queued", true);
      json += ',';
      json += rpc::string_field_json("correlation_id", queued->correlation_id);
      json += '}';
      if (auto written = rpc::write_success(output, command->id, json); !written) return written;
      continue;
    }

    if (command->type == "follow_up") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("follow_up requires message"));
            !written) {
          return written;
        }
        continue;
      }
      auto queued = rpc::queue_rpc_message(run_state.follow_up_messages, run_state, command->type, command->id,
                                           *command->message);
      if (!queued) {
        if (auto written = rpc::write_error(output, command->id, queued.error()); !written) return written;
        continue;
      }
      if (auto written = rpc::write_queue_event(output, session, session_mutex, "follow_up_queued", *queued);
          !written) {
        return written;
      }
      continue;
    }

    if (command->type == "cancel") {
      bool was_active = false;
      std::string active_request_id;
      rpc::ClearedRpcQueues cleared;
      {
        std::lock_guard lock(run_state.mutex);
        run_state.cancel_requested.store(true, std::memory_order_relaxed);
        was_active = run_state.active_run;
        active_request_id = run_state.active_request_id;
        cleared.steering_messages.reserve(run_state.steering_messages.size());
        while (!run_state.steering_messages.empty()) {
          cleared.steering_messages.push_back(std::move(run_state.steering_messages.front()));
          run_state.steering_messages.pop_front();
        }
        cleared.follow_up_messages.reserve(run_state.follow_up_messages.size());
        while (!run_state.follow_up_messages.empty()) {
          cleared.follow_up_messages.push_back(std::move(run_state.follow_up_messages.front()));
          run_state.follow_up_messages.pop_front();
        }
      }
      static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
      if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, cleared, "canceled");
          !written) {
        return written;
      }
      auto cancel_event = rpc::resolver_event_envelope(
          "cancel_requested", command->id, active_request_id.empty() ? command->id : active_request_id,
          rpc::session_id_snapshot(session, session_mutex),
          rpc::cancel_requested_payload_json(was_active, cleared.steering_messages.size(),
                                             cleared.follow_up_messages.size(), active_request_id));
      if (auto written = rpc::write_record(output, serialize_event_envelope_jsonl(cancel_event)); !written) {
        return written;
      }
      std::string json = "{";
      json += rpc::bool_field_json("cancel_requested", true);
      json += ',';
      json += rpc::bool_field_json("active_run", was_active);
      json += ',';
      json += rpc::number_field_json("cleared_steer", cleared.steering_messages.size());
      json += ',';
      json += rpc::number_field_json("cleared_follow_up", cleared.follow_up_messages.size());
      json += '}';
      if (auto written = rpc::write_success(output, command->id, json); !written) return written;
      if (auto written = rpc::write_follow_up_errors(output, cleared.follow_up_messages, "canceled"); !written) {
        return written;
      }
      continue;
    }

    if (command->type == "context" || command->type == "export" || command->type == "export_html" ||
        command->type == "compact" ||
        rpc::is_plugin_rpc_command(command->type) || rpc::is_mcp_rpc_command(command->type)) {
      bool compact_active_run = false;
      {
        std::lock_guard lock(run_state.mutex);
        if (run_state.active_run) {
          if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
              !written) {
            return written;
          }
          continue;
        }
        if (command->type == "compact") {
          run_state.active_run = true;
          run_state.active_request_id = command->id;
          run_state.cancel_requested.store(false, std::memory_order_relaxed);
          compact_active_run = true;
        }
      }
      auto clear_compact_active_run = [&] {
        if (compact_active_run) {
          rpc::set_active_run(run_state, false);
          compact_active_run = false;
        }
      };
      auto write_command_error = [&](ava::core::Error error) -> ava::core::VoidResult {
        clear_compact_active_run();
        return rpc::write_error(output, command->id, std::move(error));
      };
      auto write_command_success = [&](std::string json) -> ava::core::VoidResult {
        clear_compact_active_run();
        return rpc::write_success(output, command->id, std::move(json));
      };
      std::string slash_command;
      if (command->type == "context") {
        slash_command = "/context";
      } else if (command->type == "export") {
        slash_command = "/export";
      } else if (command->type == "export_html") {
        slash_command = "/export html";
        if (command->output_path && !command->output_path->empty()) slash_command += " " + *command->output_path;
      } else if (command->type == "compact") {
        slash_command = "/compact";
        if (command->instructions) slash_command += " " + *command->instructions;
      } else if (rpc::is_plugin_rpc_command(command->type)) {
        auto plugin_command = rpc::plugin_rpc_slash_command(*command);
        if (!plugin_command) {
          if (auto written = write_command_error(plugin_command.error()); !written) return written;
          continue;
        }
        slash_command = std::move(*plugin_command);
      } else {
        auto mcp_command = rpc::mcp_rpc_slash_command(*command);
        if (!mcp_command) {
          if (auto written = write_command_error(mcp_command.error()); !written) return written;
          continue;
        }
        slash_command = std::move(*mcp_command);
      }
      std::optional<RuntimeRunOptions> compact_runtime_options;
      std::optional<rpc::ProviderHandle> compact_provider;
      if (command->type == "compact") {
        ava::config::XdgPaths paths;
        std::string provider_id;
        {
          std::lock_guard lock(session_mutex);
          paths = session.paths;
          provider_id = session.model.provider_id;
          auto selected_provider = rpc::provider_for_session_model(session, injected_provider_id, provider);
          if (!selected_provider) {
            if (auto written = write_command_error(selected_provider.error()); !written) return written;
            continue;
          }
          compact_provider = std::move(*selected_provider);
        }
        auto ensured =
            rpc::ensure_prompt_runtime_options(paths, provider_id, runtime_options, auth_transport, "compact");
        if (!ensured) {
          if (auto written = write_command_error(ensured.error()); !written) return written;
          continue;
        }
        compact_runtime_options = std::move(*ensured);
      }
      EventBus event_bus;
      rpc::subscribe_event_envelope_writer(event_bus, output);
      std::function<bool()> compact_cancel_requested;
      if (command->type == "compact") {
        auto base_cancel_requested = runtime_options.cancel_requested;
        compact_cancel_requested = [&run_state, base_cancel_requested] {
          return rpc::cancel_requested(run_state) || (base_cancel_requested && base_cancel_requested());
        };
        if (compact_runtime_options) compact_runtime_options->cancel_requested = compact_cancel_requested;
      }
      std::unique_lock lock(session_mutex, std::defer_lock);
      if (command->type != "compact") lock.lock();
      auto summary_generator =
          command->type == "compact"
              ? CompactionSummaryGenerator([&](std::vector<ava::session::SessionEntry> const& entries,
                                               ava::session::CompactionConfig const& config,
                                               std::string_view instructions, std::size_t estimated_tokens) {
                  return generate_compaction_summary(session, entries, config, instructions, estimated_tokens,
                                                     compact_provider->get(), transport, *compact_runtime_options);
                })
              : CompactionSummaryGenerator{};
      auto result = run_command(session, CommandRequest{.command = std::move(slash_command),
                                                        .event_sink = make_runtime_event_bus_adapter(
                                                            event_bus, rpc::rpc_event_context(command->id)),
                                                         .permission_resolver = runtime_options.permission_resolver,
                                                         .compaction_summary_generator = std::move(summary_generator),
                                                         .cancel_requested = compact_cancel_requested,
                                                         .session_mutex = &session_mutex,
                                                         .propagate_compaction_errors = command->type == "compact"});
      if (!result) {
        if (auto written = write_command_error(result.error()); !written) return written;
        continue;
      }
      if (auto written = write_command_success(rpc::command_result_json(*result)); !written) return written;
      continue;
    }

    if (command->type == "prompt") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("prompt requires message"));
            !written) {
          return written;
        }
        continue;
      }

      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }

      reap_finished_prompt();
      ava::config::XdgPaths paths;
      std::vector<ava::session::ImageAttachmentRef> image_attachments;
      bool attachment_import_failed = false;
      std::filesystem::path current_dir;
      {
        std::lock_guard lock(session_mutex);
        paths = session.paths;
        current_dir = session.current_dir.empty() ? session.workspace_dir : session.current_dir;
        image_attachments.reserve((command->attachments ? command->attachments->size() : 0U) +
                                  (command->images ? command->images->size() : 0U));
        if (command->attachments) {
          for (auto const& attachment_path : *command->attachments) {
            auto imported = ava::session::import_image_attachment(
                session.store, resolve_rpc_attachment_path(current_dir, attachment_path));
            if (!imported) {
              if (auto written = rpc::write_error(output, command->id, imported.error()); !written) {
                return written;
              }
              attachment_import_failed = true;
              break;
            }
            image_attachments.push_back(std::move(*imported));
          }
        }
        if (!attachment_import_failed && command->images) {
          for (auto const& image : *command->images) {
            auto bytes = decode_rpc_image_base64(image.data_base64);
            if (!bytes) {
              if (auto written = rpc::write_error(output, command->id, bytes.error()); !written) {
                return written;
              }
              attachment_import_failed = true;
              break;
            }
            auto imported = ava::session::import_image_attachment_bytes(session.store, *bytes,
                                                                         std::string_view(image.mime_type));
            if (!imported) {
              if (auto written = rpc::write_error(output, command->id, imported.error()); !written) {
                return written;
              }
              attachment_import_failed = true;
              break;
            }
            image_attachments.push_back(std::move(*imported));
          }
        }
      }
      if (attachment_import_failed) {
        continue;
      }
      auto prompt_runtime_options = runtime_options;
      prompt_runtime_options.image_attachments.clear();
      rpc::set_active_run(run_state, true, command->id);
      prompt_worker.emplace(rpc::make_rpc_prompt_worker(rpc::RpcPromptWorkerOptions{
          .session = session,
          .session_mutex = session_mutex,
          .output = output,
          .run_state = run_state,
          .pending_state = pending_state,
          .injected_provider = provider,
          .injected_provider_id = injected_provider_id,
          .transport = transport,
          .auth_transport = auth_transport,
          .runtime_options = std::move(prompt_runtime_options),
          .paths = std::move(paths),
          .request_id = command->id,
          .message = *command->message,
          .image_attachments = std::move(image_attachments),
      }));
      continue;
    }

    auto error = rpc::invalid_rpc("unknown RPC command type");
    error.with_context("type", command->type);
    if (auto written = rpc::write_error(output, command->id, error); !written) return written;
  }

  if (prompt_worker) {
    auto cleared = rpc::close_input_and_cancel(run_state);
    static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
    if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, cleared, "canceled"); !written) {
      return written;
    }
    if (auto written = rpc::write_follow_up_errors(output, cleared.follow_up_messages, "canceled"); !written) {
      return written;
    }
    prompt_worker.reset();
  }
  if (auto async_error = rpc::take_async_error(run_state)) return std::unexpected(std::move(*async_error));
  if (input_read_error) return std::unexpected(std::move(*input_read_error));

  return {};
}

ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options,
                                   ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                   RuntimeRunOptions runtime_options, std::istream& in, std::ostream& out)
{
  return run_rpc_loop(session, open_options, provider, transport, transport, std::move(runtime_options), in, out);
}

int run_rpc_mode(RpcModeOptions const& options, std::istream& in, std::ostream& out, std::ostream& err)
{
  auto session = open_runtime_session(options.open_options);
  if (!session) {
    err << session.error().format() << '\n';
    return 1;
  }

  RuntimeRunOptions runtime_options;
  runtime_options.permission_resolver = build_headless_permission_resolver(options.permission_policy);
  runtime_options.question_resolver = nullptr;
  runtime_options.enable_transport_retries = true;

  auto registry = ava::provider::builtin_provider_registry();
  auto provider = registry.create(session->model.provider_id);
  if (!provider) {
    err << provider.error().format() << '\n';
    return 1;
  }
  ava::provider::CurlCliTransport transport;
  auto result = run_rpc_loop(*session, options.open_options, **provider, transport, transport,
                             std::move(runtime_options), in, out);
  if (!result) {
    err << result.error().format() << '\n';
    return 1;
  }
  return 0;
}

}  // namespace ava::app
