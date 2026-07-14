#include "sys.h"
#include "ava/app/rpc_mode.h"

#include "ava/app/command_registry.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/rpc/output.h"
#include "ava/app/rpc/prompt_worker.h"
#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc/serialization_json.h"
#include "ava/app/rpc/session_commands.h"
#include "ava/app/rpc/session_operators.h"

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

// Drive the RPC JSONL protocol on `in`/`out`.
//
// This function runs on a single dedicated "reader" thread: it reads one request line at a time,
// dispatches it, and writes the response. Long-running work (a `prompt`) is offloaded to a worker
// thread stored in `prompt_worker` so that the reader stays free to service commands that talk to
// that worker (permission_reply, question_reply, steer, follow_up, cancel). `session` and its store
// are shared with the worker and are only touched under `session_mutex`; `run_state` coordinates the
// active-run lifecycle, and `pending_state` holds in-flight permission/question requests that are
// parked waiting for a client reply.
//
// Returns an error only for unrecoverable conditions (stdout irreversibly broken, or stdin failed);
// per-request errors are written back to the client and the loop continues.
ava::core::VoidResult run_rpc_loop(RuntimeSession& session, RuntimeOpenOptions const& open_options,
                                   ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                   ava::provider::Transport& auth_transport, RuntimeRunOptions runtime_options,
                                   std::istream& in, std::ostream& out)
{
  // Register per-thread debug support; run_rpc_loop is always entered on a freshly created thread.
  // This function is called first-thing after creating a thread.
  Debug(NAMESPACE_DEBUG::init_thread("run_rpc_loop"));

  DoutEntering(dc::rpc, "run_rpc_loop(" << session << ", " << open_options << ", " << provider << ", " << auth_transport
      << ", " << runtime_options << ", " << print_reference(in) << ", " << print_reference(out) << ")");

  // Coordinator for the active prompt/compact run: active_run flag, the active request id, the
  // steering/follow-up queues, the cancel flag, and the side-channel used to surface write errors
  // that occurred on the worker thread back to this reader thread.
  rpc::RpcRunState run_state;

  // Holds permission/question requests that are parked inside the worker waiting for a client reply.
  // permission_reply/question_reply (handled on this thread) resolve entries here and wake the worker.
  rpc::PendingResolverState pending_state;

  // Protect the output stream with a mutex. Every record goes out through `output`, never directly to `out`.
  //
  // If a write to the ostream fails irrecoverably we cannot communicate further with the client, so flip
  // the run state to "input closed + cancel" and wake any parked resolver requests. This unblocks the
  // worker (if any) and lets the loop reach its termination path; close_input_and_cancel returns the
  // cleared queues but we discard them here because we are already aborting.
  rpc::output_ts output(out, [&] {
      static_cast<void>(rpc::close_input_and_cancel(run_state));
      static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
    });

  // Guards all access to `session` and its store, which are shared with the prompt worker.
  std::mutex session_mutex;

  // The optional worker thread for an in-flight prompt/follow-up chain. It is empty when no run is
  // active; it must be empty before emplacing a new worker, which is why finished workers are reaped
  // (see reap_finished_prompt) before a new run is started.
  std::optional<std::jthread> prompt_worker;
  // Ensure a permission resolver exists: default to a headless policy resolver when the caller did
  // not provide one (the RPC mode never has an interactive UI to ask the user).
  if (!runtime_options.permission_resolver) {
    runtime_options.permission_resolver = build_headless_permission_resolver(HeadlessPermissionPolicyOptions{});
  }
  // Wrap the chosen resolver with a layer that also consults the session's persistent permission
  // rules on disk, so rule_add/rule_remove take effect for subsequent prompts. After this the
  // runtime_options.permission_resolver is the fully-configured resolver handed to every run.
  runtime_options.permission_resolver = ava::permissions::build_persistent_permission_rule_resolver(
      rpc::permission_rule_store_for_session(session), std::move(runtime_options.permission_resolver));
  // Snapshot the provider id the session was opened with. We only have a live Provider/Transport for
  // this id (the `provider`/`transport` arguments), so it is kept as the injected/fallback provider
  // when resolving which provider to use for a prompt or compact, even if set_model later changes the
  // session's recorded provider id.
  std::string const injected_provider_id = session.model.provider_id;

  // Join a previously started worker thread once it has finished its work. A worker clears
  // active_run when it completes (and has no more follow-ups), so "no active run + worker present"
  // means the worker is done and can be joined. This must run before starting a new prompt, because
  // prompt_worker.emplace() requires the optional to be empty.
  auto reap_finished_prompt = [&] {
    if (prompt_worker && !rpc::active_run(run_state)) {
      prompt_worker.reset();
    }
  };

  // Reused buffer for each incoming request line.
  std::string line;
  // Set only when reading stdin fails with an I/O error; carrying it out of the loop lets us return
  // it after joining the worker. (Oversized-line errors are not fatal and are handled inline.)
  std::optional<ava::core::Error> input_read_error;
  while (true) {
    // Read one newline-terminated request line into `line`. Result is Result<bool>:
    //   *true        -> a complete line was read into `line`;
    //   *false       -> clean EOF on an empty line (the client closed stdin normally);
    //   error        -> either InvalidArgument (line exceeded kMaxRpcLineBytes) or Io (read failure).
    auto read_line = rpc::read_rpc_line_bounded(in, line);
    if (!read_line) {
      // An oversized line is a malformed request, not a stream failure: report it to the client
      // (with an empty id since we could not trust/keep the content) and keep reading.
      if (read_line.error().category() == ava::core::ErrorCategory::InvalidArgument) {
        if (auto written = rpc::write_error(output, "", read_line.error()); !written) return written;
        continue;
      }
      // Any other read error is unrecoverable; stash it and exit the loop so we can join the worker
      // and return the error after cleanup.
      input_read_error = std::move(read_line.error());
      break;
    }
    // Clean EOF with no partial line: the client closed the stream; fall through to shutdown.
    if (!*read_line) break;
    // The reader thread is idle now (between requests), so if the previous worker has finished, join
    // it here. Doing it before dispatching also ensures prompt_worker is empty for a new prompt.
    reap_finished_prompt();
    // If the worker hit an unrecoverable write error during the last run it recorded it in run_state;
    // surface it now and terminate, since continuing would risk losing further responses silently.
    if (auto async_error = rpc::take_async_error(run_state)) return std::unexpected(std::move(*async_error));
    // `line` now holds one client JSON request; parse it into the RpcCommand aggregate.
    auto command = parse_rpc_command_line(line);
    if (!command) {
      // Malformed JSON. Try to recover the "id" field from the raw line so the client can correlate
      // the error reply; if that fails the id is empty. Then keep reading.
      if (auto written = rpc::write_error(output, rpc::parse_error_response_id(line), command.error()); !written) {
        return written;
      }
      continue;
    }
    // Reject unsupported protocol versions before doing anything else; a version mismatch means we
    // cannot safely interpret the rest of the command's fields.
    if (auto valid_version = rpc::validate_protocol_version(*command); !valid_version) {
      if (auto written = rpc::write_error(output, command->id, valid_version.error()); !written) return written;
      continue;
    }

    // get_protocol: report the protocol/session-entry versions we speak. Handled inline because it is
    // the handshake command and must work regardless of session/run state.
    if (command->type == "get_protocol") {
      if (auto written = rpc::write_success(output, command->id, rpc::rpc_protocol_result_json()); !written) {
        return written;
      }
      continue;
    }

    // Delegate the large family of session query/mutation commands (get_state, list_sessions,
    // set_model, fork_session, permission_rule_*, etc.) to the session-command dispatcher. It returns
    // Result<bool>: the bool is true if it recognized and fully handled the command (including writing
    // the response), false if the command type is not one of its known types (so we keep dispatching).
    // An error result means a write failed irrecoverably and the loop must terminate.
    auto session_command =
        rpc::handle_session_rpc_command(rpc::RpcSessionCommandContext{.command = *command,
                                                                      .session = session,
                                                                      .open_options = open_options,
                                                                      .output = output,
                                                                      .run_state = run_state,
                                                                      .session_mutex = session_mutex});
    if (!session_command) return std::unexpected(std::move(session_command.error()));
    // Handled here -> done with this request; read the next line.
    if (*session_command) continue;

    // permission_grants: list the in-memory session-scoped grants accumulated via allow_session replies.
    if (command->type == "permission_grants") {
      if (auto written =
              rpc::write_success(output, command->id, rpc::permission_session_grants_result_json(pending_state));
          !written) {
        return written;
      }
      continue;
    }

    // permission_grant_revoke: drop one session grant by id. Requires grant_id.
    if (command->type == "permission_grant_revoke") {
      if (!command->grant_id || command->grant_id->empty()) {
        if (auto written =
                rpc::write_error(output, command->id, rpc::invalid_rpc("permission_grant_revoke requires grant_id"));
            !written) {
          return written;
        }
        continue;
      }
      // Returns the JSON of the removed grant on success, or an error if the id was unknown.
      auto revoked = rpc::permission_session_grant_revoke_result_json(pending_state, *command->grant_id);
      if (!revoked) {
        if (auto written = rpc::write_error(output, command->id, revoked.error()); !written) return written;
        continue;
      }
      // First emit a permission_grant_revoked event so observers learn the grant is gone, then answer
      // the request itself with the same payload. correlation_id = request id ties the event to this call.
      auto envelope = rpc::resolver_event_envelope("permission_grant_revoked", command->id, command->id,
                                                   rpc::session_id_snapshot(session, session_mutex), *revoked);
      if (auto written = rpc::Output::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, *revoked); !written) return written;
      continue;
    }

    // permission_grants_clear: drop all session grants at once, returning how many were cleared.
    if (command->type == "permission_grants_clear") {
      auto const cleared = rpc::permission_session_grants_clear_result_json(pending_state);
      // Emit the grants-cleared event first, then the success response (same payload).
      auto envelope = rpc::resolver_event_envelope("permission_grants_cleared", command->id, command->id,
                                                   rpc::session_id_snapshot(session, session_mutex), cleared);
      if (auto written = rpc::Output::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, cleared); !written) return written;
      continue;
    }

    // list_commands: enumerate the available slash-commands/prompts/skills for this session. Rejected
    // while a run is active because building the registry may inspect session/plugin state that the
    // worker is mutating concurrently.
    if (command->type == "list_commands") {
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }
      // Build the registry under the session lock since it inspects session/plugin/mcp state. The
      // cancel probe reads the relaxed cancel flag (mostly relevant for consistency if a cancel raced).
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

    // invoke_command: run a slash-command by name (optionally with arguments). Most slash-commands run
    // to completion inline; some (e.g. a prompt alias) yield a prompt_message, in which case we promote
    // this into a full prompt run on the worker.
    if (command->type == "invoke_command") {
      if (!command->name || command->name->empty()) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("invoke_command requires name"));
            !written) {
          return written;
        }
        continue;
      }
      // Cannot start command work while a prompt/compact is already running.
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }

      // Reconstruct the slash command string: ensure a leading '/', then append the raw arguments.
      std::string slash_command = command->name->starts_with('/') ? *command->name : "/" + *command->name;
      if (command->command_arguments && !command->command_arguments->empty()) {
        slash_command += " " + *command->command_arguments;
      }

      // Events emitted by the command are funneled through this short-lived bus and written to stdout
      // as event envelopes (correlated to this request id via rpc_event_context).
      EventBus event_bus;
      rpc::subscribe_event_envelope_writer(event_bus, output);
      std::optional<std::string> prompt_message;
      ava::config::XdgPaths paths;
      {
        // Run the command under the session lock: it may read/mutate session state directly.
        std::lock_guard lock(session_mutex);
        paths = session.paths;
        auto result = run_command(session, CommandRequest{.command = std::move(slash_command),
                                                          .event_sink = make_runtime_event_bus_adapter(
                                                              event_bus, rpc::rpc_event_context(command->id)),
                                                          .permission_resolver = runtime_options.permission_resolver,
                                                          .session_mutex = &session_mutex});
        if (!result) {
          // Command execution failed: report it and keep reading.
          if (auto written = rpc::write_error(output, command->id, result.error()); !written) return written;
          continue;
        }
        if (!result->handled) {
          // No slash-command matched the name: surface "command not found" with the requested name.
          auto error = rpc::invalid_rpc("command not found");
          error.with_context("name", *command->name);
          if (auto written = rpc::write_error(output, command->id, error); !written) return written;
          continue;
        }
        if (result->prompt_message) {
          // The command resolved to a prompt message: fall out of the lock and start a worker run
          // with that message (handled below).
          prompt_message = *result->prompt_message;
        } else {
          // Plain command: it produced output already; write the command result and we are done.
          if (auto written = rpc::write_success(output, command->id, rpc::command_result_json(*result)); !written) {
            return written;
          }
          continue;
        }
      }

      // We get here only when invoke_command resolved to a prompt. Reap any previously finished worker
      // (prompt_worker must be empty before emplace), mark the run active under this request id, and
      // spawn the worker. The worker writes events during the run and the final success/error itself.
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

    // permission_reply: the client answers a permission_requested event emitted by the worker. The
    // request_id identifies the pending permission request; correlation_id ties it to the originating
    // prompt; decision is allow/allow_session/deny. Resolving wakes the parked worker; this command
    // itself just acknowledges with "{}".
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
      // Apply the decision to the matching pending request; on success this notify_all()s the worker.
      auto resolved = rpc::resolve_permission_reply(pending_state, *command->request_id, *command->correlation_id,
                                                    *command->decision, command->reason);
      if (!resolved) {
        if (auto written = rpc::write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      // Emit the resolver-side permission_replied event (correlated to the originating prompt) so
      // observers learn a reply was delivered, then answer the request itself with an empty object.
      auto envelope = rpc::resolver_event_envelope(
          "permission_replied", *command->correlation_id, *command->correlation_id,
          rpc::session_id_snapshot(session, session_mutex),
          rpc::permission_reply_payload_json(*command->request_id, *command->decision, command->reason));
      if (auto written = rpc::Output::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    // question_reply: same pattern as permission_reply but for question_requested events. The answer
    // is conveyed via answer (free text) or selected/selected_options (chosen option values).
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
      // Validate and apply the answer, then wake the parked worker.
      auto resolved = rpc::resolve_question_reply(pending_state, *command->request_id, *command->correlation_id,
                                                  command->answer, command->selected, command->selected_options);
      if (!resolved) {
        if (auto written = rpc::write_error(output, command->id, resolved.error()); !written) return written;
        continue;
      }
      // Emit question_replied (correlated to the prompt) then answer the request with "{}".
      auto envelope =
          rpc::resolver_event_envelope("question_replied", *command->correlation_id, *command->correlation_id,
                                       rpc::session_id_snapshot(session, session_mutex),
                                       rpc::question_reply_payload_json(*command->request_id, command->answer,
                                                                        command->selected, command->selected_options));
      if (auto written = rpc::Output::write_record(output, serialize_event_envelope_jsonl(envelope)); !written) return written;
      if (auto written = rpc::write_success(output, command->id, "{}"); !written) return written;
      continue;
    }

    // steer: append an interjection to the steering queue for the active prompt. Steering messages are
    // injected into the model mid-run at the next safe point. Acknowledged immediately with the
    // correlation id; the worker later emits steer_applied/steer_skipped for each.
    if (command->type == "steer") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("steer requires message"));
            !written) {
          return written;
        }
        continue;
      }
      // queue_rpc_message rejects when input is closed or no run is active that can consume the steer.
      auto queued =
          rpc::queue_rpc_message(run_state.steering_messages, run_state, command->type, command->id, *command->message);
      if (!queued) {
        if (auto written = rpc::write_error(output, command->id, queued.error()); !written) return written;
        continue;
      }
      // Notify observers the message was queued, then reply with the assigned correlation id.
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

    // follow_up: queue a message to run as a new prompt after the current one completes. Unlike steer,
    // follow_up gets no inline success response here: its eventual result is delivered later as a
    // separate success/error keyed by its own request id (the worker emits follow_up_started first).
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
      // The follow_up_queued event is the only immediate confirmation the client receives.
      if (auto written = rpc::write_queue_event(output, session, session_mutex, "follow_up_queued", *queued);
          !written) {
        return written;
      }
      continue;
    }

    // cancel: request cancellation of the active prompt/compact, and tear down everything queued for it.
    // Always succeeds (it is even valid when nothing is running); the response reports what was cleared.
    if (command->type == "cancel") {
      bool was_active = false;
      std::string active_request_id;
      rpc::ClearedRpcQueues cleared;
      {
        // Take the run-state lock once and do all coordination atomically: set the cancel flag the
        // worker polls, snapshot whether a run was active and its id, and drain both queues into
        // `cleared` so they can be reported as skipped.
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
      // Wake any resolver requests the worker is parked on, denying/canceling them, so the worker can
      // observe the cancel flag and unwind. Result ignored: whether anything was pending is irrelevant.
      static_cast<void>(rpc::cancel_pending_resolvers(pending_state));
      // Emit steer_skipped/follow_up_skipped events for every drained queue entry (reason "canceled").
      if (auto written = rpc::write_skipped_queue_events(output, session, session_mutex, cleared, "canceled");
          !written) {
        return written;
      }
      // Emit the cancel_requested event, correlated to the run being canceled (or to this command if
      // nothing was active). Reports whether a run was active and how many queues were cleared.
      auto cancel_event = rpc::resolver_event_envelope(
          "cancel_requested", command->id, active_request_id.empty() ? command->id : active_request_id,
          rpc::session_id_snapshot(session, session_mutex),
          rpc::cancel_requested_payload_json(was_active, cleared.steering_messages.size(),
                                             cleared.follow_up_messages.size(), active_request_id));
      if (auto written = rpc::Output::write_record(output, serialize_event_envelope_jsonl(cancel_event)); !written) {
        return written;
      }
      // The cancel command's own success response.
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
      // Each cleared follow-up had promised a future result; deliver a canceled error to each now,
      // keyed by its own request id, so clients waiting on them get a definitive terminal response.
      if (auto written = rpc::write_follow_up_errors(output, cleared.follow_up_messages, "canceled"); !written) {
        return written;
      }
      continue;
    }

    // Shared handler for the slash-command passthroughs: context, export, export_html, compact, and the
    // plugin/mcp commands. Each is translated into a "/..." string and run via run_command; events flow
    // through a per-request EventBus. compact is special: it is the only one here that runs agent/model
    // work, so it runs on THIS thread but must hold the active_run flag (so it counts as a run for
    // gating and cancellation) and supplies a compaction summary generator.
    if (command->type == "context" || command->type == "export" || command->type == "export_html" ||
        command->type == "compact" ||
        rpc::is_plugin_rpc_command(command->type) || rpc::is_mcp_rpc_command(command->type)) {
      bool compact_active_run = false;
      {
        // Atomically check active_run and, for compact, claim it. Other commands in this group only
        // need the rejection check; compact additionally marks itself as the active run and resets the
        // cancel flag so a prior cancel does not bleed into this new operation.
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
      // compact must release the active_run flag on every exit path (success or error); the helpers
      // below centralize that so no branch forgets to clear it.
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
      // Translate the RPC command into the slash command string. plugin/mcp helpers validate their
      // required fields (plugin_id/server_id/name) and may return an error.
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
      // compact needs a provider and fully-resolved runtime options because it calls the model to
      // produce the compaction summary; resolve them up front (under the session lock for provider).
      std::optional<RuntimeRunOptions> compact_runtime_options;
      std::optional<rpc::ProviderHandle> compact_provider;
      if (command->type == "compact") {
        ava::config::XdgPaths paths;
        std::string provider_id;
        {
          std::lock_guard lock(session_mutex);
          paths = session.paths;
          provider_id = session.model.provider_id;
          // Pick the provider handle: the injected one if it matches, else from the registry.
          auto selected_provider = rpc::provider_for_session_model(session, injected_provider_id, provider);
          if (!selected_provider) {
            if (auto written = write_command_error(selected_provider.error()); !written) return written;
            continue;
          }
          compact_provider = std::move(*selected_provider);
        }
        // Finalize the runtime options (e.g. ensure auth/transport are wired) for the compact run.
        auto ensured =
            rpc::ensure_prompt_runtime_options(paths, provider_id, runtime_options, auth_transport, "compact");
        if (!ensured) {
          if (auto written = write_command_error(ensured.error()); !written) return written;
          continue;
        }
        compact_runtime_options = std::move(*ensured);
      }
      // All events from the slash command are written as event envelopes correlated to this request.
      EventBus event_bus;
      rpc::subscribe_event_envelope_writer(event_bus, output);
      // compact's cancel probe combines this run's cancel flag with any base cancel probe.
      std::function<bool()> compact_cancel_requested;
      if (command->type == "compact") {
        auto base_cancel_requested = runtime_options.cancel_requested;
        compact_cancel_requested = [&run_state, base_cancel_requested] {
          return rpc::cancel_requested(run_state) || (base_cancel_requested && base_cancel_requested());
        };
        if (compact_runtime_options) compact_runtime_options->cancel_requested = compact_cancel_requested;
      }
      // compact runs on this thread and is NOT a separate worker, so it cannot hold session_mutex for
      // its whole duration (the worker model isn't involved). The non-compact slash-commands are quick
      // and operate under the session lock. Hence: lock for non-compact, defer (leave unlocked) for
      // compact so it can release/reacquire around the model call as needed.
      std::unique_lock lock(session_mutex, std::defer_lock);
      if (command->type != "compact") lock.lock();
      // For compact, provide a summary generator that invokes the model via the chosen provider; for
      // other commands pass an empty generator (they do not compact).
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
        // Command failed (compact surfaces its errors here when propagate_compaction_errors is set).
        if (auto written = write_command_error(result.error()); !written) return written;
        continue;
      }
      // Success: write the command result (and clear active_run if this was a compact).
      if (auto written = write_command_success(rpc::command_result_json(*result)); !written) return written;
      continue;
    }

    // prompt: the main agent-run command. Send a user message to the model and stream events as the
    // agent works. Because this is long-running, it is the canonical worker-spawning command: we set
    // up state on this thread, then hand off to a prompt_worker jthread and keep reading stdin so the
    // client can send permission_reply/steer/cancel/etc. for the run.
    if (command->type == "prompt") {
      if (!command->message) {
        if (auto written = rpc::write_error(output, command->id, rpc::invalid_rpc("prompt requires message"));
            !written) {
          return written;
        }
        continue;
      }

      // Only one prompt/compact may be active at a time.
      if (rpc::active_run(run_state)) {
        if (auto written = rpc::write_error(output, command->id, rpc::active_run_reject_error(command->type));
            !written) {
          return written;
        }
        continue;
      }

      // Join any previously finished worker before starting a new one (prompt_worker must be empty).
      reap_finished_prompt();
      ava::config::XdgPaths paths;
      std::vector<ava::session::ImageAttachmentRef> image_attachments;
      bool attachment_import_failed = false;
      std::filesystem::path current_dir;
      {
        // Under the session lock: snapshot paths/current_dir and import all attachments into the
        // session store. Attachments come either as file paths (read from disk) or inline base64
        // images; both are normalized into ImageAttachmentRef entries handed to the worker. A failure
        // importing any attachment aborts the whole prompt (we must not start a run we cannot feed).
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
            // Decode the inline base64 image payload (also enforces size limits).
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
      // An attachment failed: the error was already written above; just go read the next request.
      if (attachment_import_failed) {
        continue;
      }
      // Give the worker its own copy of the runtime options, clearing any stale image attachments on
      // the copy (we pass attachments explicitly below), then mark the run active and spawn the worker.
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

    // Nothing matched: report an unknown command type. We get here only if every handler above
    // returned false/did not continue, so `command` is well-formed but of an unrecognized type.
    auto error = rpc::invalid_rpc("unknown RPC command type");
    error.with_context("type", command->type);
    if (auto written = rpc::write_error(output, command->id, error); !written) return written;
  }

  // --- Shutdown path (reached on clean EOF or an unrecoverable stdin error) ---
  // If a worker is still running, forcibly wind it down: mark input closed + cancel, wake any parked
  // resolver requests, emit skipped events for whatever was still queued, and deliver canceled errors
  // for follow-ups that will now never run. Then join the worker by resetting the optional.
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
  // If the worker recorded an unrecoverable write error during shutdown, surface it.
  if (auto async_error = rpc::take_async_error(run_state)) return std::unexpected(std::move(*async_error));
  // Surface the stdin read error that caused us to leave the loop (if any).
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
