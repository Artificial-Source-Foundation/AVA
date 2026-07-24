#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/test_harness.h"
#include "tests/support/tui_test_support.h"
#include "ava/diagnostics/runtime_diagnostics.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_palette.h"
#include "ava/app/command_sessions.h"
#include "ava/app/commands.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/OpenOptions.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/agent_loop.h"
#include "ava/session/assistant_output.h"
#include "ava/session/attachments.h"
#include "ava/session/export.h"
#include "ava/session/record.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/validation.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

void test_app_session_jsonl_import_export_portable_attachments()
{
  auto const root = create_empty_root("app-session-jsonl-portable-attachments");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.continuity.workspace_dir = workspace;
  open_options.continuity.current_dir = workspace;
  open_options.continuity.mode = ava::agent::Mode::Build;
  open_options.continuity.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "portable JSONL attachment test opens runtime session");
  if (!session)
    return;

  auto const attachment_json = std::string(R"({"id":"img_portable","type":"image","mime_type":"image/png","byte_size":12,)"
                                           R"("sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)"
                                           R"("storage_path":"attachments/source-only.png"})");
  auto const attached_entry = ava::session::SessionEntry{.id = "entry_jsonl_attachment_user",
                                                         .parent_id = "",
                                                         .type = ava::session::EntryType::UserMessage,
                                                         .timestamp = "2026-05-02T00:00:01Z",
                                                         .data_json = "{\"text\":\"see attached\",\"attachments\":[" + attachment_json + "]}"};
  auto appended = session->append_owned(attached_entry);
  expect(appended.has_value(), "portable JSONL attachment test seeds non-redacted image attachment metadata");
  if (!appended)
    return;

  auto stdout_export = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export jsonl"});
  auto const stdout_jsonl = stdout_export && !stdout_export->output.empty() ? stdout_export->output.front() : std::string{};
  auto file_export = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export jsonl attachment-export.jsonl"});
  auto const file_jsonl = app_read_binary_file(workspace / "attachment-export.jsonl");
  expect(stdout_export && stdout_export->handled && file_export && file_export->handled && !file_export->output.empty() && stdout_jsonl == file_jsonl &&
             file_jsonl.find("attachments/source-only.png") == std::string::npos && file_jsonl.find("attachments/portable-redacted") != std::string::npos &&
             file_jsonl.find("\"redacted\":true") != std::string::npos && file_export->output[0].find("note:") == std::string::npos,
         "pathless and file portable JSONL exports deterministically redact attachment references without a contradictory warning");

  auto write_import_entries = [](std::filesystem::path const& path, std::vector<ava::session::SessionEntry> const& entries) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    for (auto const& entry : entries)
    {
      auto line = ava::session::serialize_session_entry_line(entry);
      expect(line.has_value(), "portable JSONL attachment import test serializes fixture entry");
      if (line)
        file << *line << '\n';
    }
  };
  auto const import_start = ava::session::SessionEntry{.id = "entry_import_attachment_start",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::SessionStart,
                                                       .timestamp = "2026-05-02T00:00:00Z",
                                                       .data_json = "{\"mode\":\"build\",\"provider\":\"openai\",\"model\":\"gpt-5.5\"}"};
  write_import_entries(workspace / "nonportable-attachment.jsonl", {import_start, attached_entry});
  auto const session_before_nonportable_import = session->store.session_id();
  auto nonportable_import = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/import nonportable-attachment.jsonl --confirm"});
  expect(nonportable_import && nonportable_import->handled && !nonportable_import->output.empty() &&
             nonportable_import->output[0].find("non-redacted image attachment metadata") != std::string::npos &&
             session->store.session_id() == session_before_nonportable_import,
         "direct non-portable attachment references remain rejected rather than creating dangling bytes");

  auto imported = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/import attachment-export.jsonl --confirm"});
  auto imported_entries = session->store.load();
  expect(imported && imported->handled && !imported->output.empty() && imported->output[0].find("imported session") != std::string::npos && imported_entries &&
             std::ranges::any_of(*imported_entries,
                                 [](ava::session::SessionEntry const& entry) {
                                   return entry.data_json.find("img_portable") != std::string::npos &&
                                          entry.data_json.find("\"redacted\":true") != std::string::npos &&
                                          entry.data_json.find("attachments/source-only.png") == std::string::npos;
                                 }),
         "portable JSONL attachment exports re-import as redacted metadata without source bytes");
}

void test_app_session_jsonl_export_sanitizes_private_reasoning_replay_metadata()
{
  auto const root = create_empty_root("app-session-jsonl-private-replay-export");
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.continuity.workspace_dir = workspace;
  open_options.continuity.current_dir = workspace;
  open_options.continuity.mode = ava::agent::Mode::Build;
  open_options.continuity.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "private JSONL export test opens a runtime session");
  if (!session)
    return;

  auto appended = session->append_owned(ava::session::SessionEntry{
      .id = "entry_private_reasoning_export",
      .parent_id = "",
      .type = ava::session::EntryType::ReasoningBlock,
      .timestamp = "2026-05-10T00:00:01Z",
      .data_json = "{\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"format\":\"openai_responses\",\"text\":\"visible reasoning summary\","
                   "\"redacted\":false,\"signature\":\"export-private-signature\",\"redacted_data\":\"export-private-redacted\","
                   "\"native_item_json\":\"{\\\"id\\\":\\\"rs_export\\\",\\\"type\\\":\\\"reasoning\\\",\\\"summary\\\":[],"
                   "\\\"encrypted_content\\\":\\\"export-private-cipher\\\"}\"}"});
  expect(appended.has_value(), "private JSONL export test seeds native reasoning replay metadata");
  if (!appended)
    return;

  auto stdout_export = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export jsonl"});
  auto const stdout_jsonl = stdout_export && !stdout_export->output.empty() ? stdout_export->output.front() : std::string{};
  expect(stdout_export && stdout_export->handled && stdout_jsonl.find("visible reasoning summary") != std::string::npos &&
             stdout_jsonl.find("private_replay_metadata_omitted") != std::string::npos && stdout_jsonl.find("export-private-signature") == std::string::npos &&
             stdout_jsonl.find("export-private-redacted") == std::string::npos && stdout_jsonl.find("export-private-cipher") == std::string::npos,
         "command dispatcher /export jsonl stdout removes private reasoning replay values while preserving visible portable content");

  auto file_export = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export jsonl private-export.jsonl"});
  auto const export_path = workspace / "private-export.jsonl";
  auto const file_jsonl = app_read_binary_file(export_path);
  expect(file_export && file_export->handled && !file_export->output.empty() && file_export->output.front().find("format: jsonl") != std::string::npos &&
             file_jsonl.find("visible reasoning summary") != std::string::npos && file_jsonl.find("private_replay_metadata_omitted") != std::string::npos &&
             file_jsonl.find("export-private-signature") == std::string::npos && file_jsonl.find("export-private-redacted") == std::string::npos &&
             file_jsonl.find("export-private-cipher") == std::string::npos,
         "command dispatcher /export jsonl file removes all private reasoning replay values");

  auto source_entries = session->store.load();
  expect(source_entries && std::ranges::any_of(*source_entries,
                                               [](ava::session::SessionEntry const& entry) {
                                                 return entry.id == "entry_private_reasoning_export" &&
                                                        entry.data_json.find("export-private-signature") != std::string::npos &&
                                                        entry.data_json.find("export-private-redacted") != std::string::npos &&
                                                        entry.data_json.find("export-private-cipher") != std::string::npos;
                                               }),
         "private JSONL export leaves active session reasoning metadata unchanged");

  auto imported = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/import private-export.jsonl --confirm"});
  auto imported_entries = session->store.load();
  expect(imported && imported->handled && !imported->output.empty() && imported->output.front().find("imported session") != std::string::npos &&
             imported_entries &&
             std::ranges::any_of(*imported_entries,
                                 [](ava::session::SessionEntry const& entry) {
                                   return entry.type == ava::session::EntryType::ReasoningBlock &&
                                          entry.data_json.find("visible reasoning summary") != std::string::npos &&
                                          entry.data_json.find("private_replay_metadata_omitted") != std::string::npos &&
                                          entry.data_json.find("export-private-signature") == std::string::npos &&
                                          entry.data_json.find("export-private-redacted") == std::string::npos &&
                                          entry.data_json.find("export-private-cipher") == std::string::npos;
                                 }),
         "sanitized JSONL export remains importable without restoring private reasoning replay values");
}

void test_app_session_branch_commands()
{
  auto const root = create_empty_root("app-session-branch-commands");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.continuity.workspace_dir = workspace;
  open_options.continuity.current_dir = workspace;
  open_options.continuity.mode = ava::agent::Mode::Build;
  open_options.continuity.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "slash branch command test opens runtime session");
  if (!session)
    return;

  auto const source_session_id = session->store.session_id();
  auto seed = session->append_owned(ava::session::SessionEntry{.id = "entry_branch_seed",
                                                               .parent_id = "",
                                                               .type = ava::session::EntryType::UserMessage,
                                                               .timestamp = "2026-05-07T00:00:00Z",
                                                               .data_json = "{\"text\":\"seed\"}"});
  expect(seed.has_value(), "slash branch command test seeds source entry");
  auto const source_path = session->store.session_path();
  auto const valid_source_bytes = app_read_binary_file(source_path);
  {
    std::ofstream torn_source(source_path, std::ios::binary | std::ios::app);
    torn_source << "{\"version\":3,\"id\":\"command-fork-torn";
  }

  auto forked = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/fork Review branch"});
  auto const fork_session_id = session->store.session_id();
  auto fork_metadata = ava::session::load_session_metadata(session->store);
  expect(forked && forked->handled && !forked->output.empty() && fork_session_id != source_session_id &&
             forked->output[0].find("forked session " + fork_session_id) != std::string::npos &&
             forked->output[0].find("from " + source_session_id) != std::string::npos &&
             forked->output[0].find("switched to " + fork_session_id) != std::string::npos && fork_metadata && fork_metadata->name == "Review branch" &&
             fork_metadata->parent_session_id == source_session_id && fork_metadata->source_session_id == source_session_id &&
             fork_metadata->branch_from_entry_id == "entry_branch_seed" && fork_metadata->branch_origin == "fork" && fork_metadata->actor == "tui" &&
             app_read_binary_file(source_path) == valid_source_bytes,
         "slash /fork recovers its actively leased source, creates an append-only branch, persists provenance metadata, and switches runtime session");
  auto fork_contender = ava::session::SessionLease::acquire(session->store.session_path());
  expect(!fork_contender && fork_contender.error().message().find("already owned") != std::string::npos,
         "slash /fork transfers the destination lease directly into the replacement runtime");

  auto const fork_path = session->store.session_path();
  auto const valid_fork_bytes = app_read_binary_file(fork_path);
  {
    std::ofstream torn_fork(fork_path, std::ios::binary | std::ios::app);
    torn_fork << "{\"version\":3,\"id\":\"command-clone-torn";
  }
  auto cloned = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/clone Full copy"});
  auto const clone_session_id = session->store.session_id();
  auto clone_metadata = ava::session::load_session_metadata(session->store);
  auto clone_entries = session->store.load();
  expect(cloned && cloned->handled && !cloned->output.empty() && clone_session_id != fork_session_id &&
             cloned->output[0].find("cloned session " + clone_session_id) != std::string::npos &&
             cloned->output[0].find("from " + fork_session_id) != std::string::npos &&
             cloned->output[0].find("switched to " + clone_session_id) != std::string::npos && clone_metadata && clone_metadata->name == "Full copy" &&
             clone_metadata->parent_session_id == fork_session_id && clone_metadata->source_session_id == fork_session_id &&
             clone_metadata->branch_origin == "clone" && clone_metadata->actor == "tui" && clone_entries && clone_entries->size() >= 3 &&
             app_read_binary_file(fork_path) == valid_fork_bytes,
         "slash /clone recovers its actively leased source, copies the full branch, persists provenance, and switches runtime session");
  auto clone_contender = ava::session::SessionLease::acquire(session->store.session_path());
  expect(!clone_contender && clone_contender.error().message().find("already owned") != std::string::npos,
         "slash /clone transfers the destination lease directly into the replacement runtime");

  auto sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/tree Full copy"});
  expect(sessions && sessions->handled && !sessions->output.empty() && sessions->output[0].find("Full copy") != std::string::npos &&
             sessions->output[0].find("origin=clone") != std::string::npos,
         "slash /tree alias exposes newly cloned branch in the session tree");
}

void test_app_session_new_resume_commands()
{
  auto const root = create_empty_root("app-session-new-resume-commands");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  auto const additional_writable_dir = root / "additional-writable";
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(additional_writable_dir);
  auto diagnostics = ava::diagnostics::RuntimeDiagnostics::create(paths, false);
  expect(diagnostics.has_value(), "slash new/resume continuity test creates diagnostics");
  if (!diagnostics)
    return;

  ava::app::runtime::OpenOptions open_options;
  open_options.continuity.workspace_dir = workspace;
  open_options.continuity.current_dir = workspace;
  open_options.continuity.additional_writable_dirs = {additional_writable_dir};
  open_options.continuity.mode = ava::agent::Mode::Build;
  open_options.continuity.paths = paths;
  open_options.continuity.prompt_overrides.system_prompt = "continuity prompt override";
  open_options.continuity.offline = true;
  open_options.continuity.session_read_limits = {.max_file_bytes = 8192, .max_line_bytes = 2048, .max_entries = 32};
  open_options.continuity.diagnostics = *diagnostics;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "slash new/resume command test opens runtime session");
  if (!session)
    return;

  session->continuity.default_model_override = session->model;
  session->continuity.pin_model_override = true;
  auto const expected_anchor_set = session->continuity.anchor_set;
  auto const expected_diagnostics = session->continuity.diagnostics;
  auto const expected_pinned_model_id = session->model.model_id;
  auto continuity_preserved = [&](ava::app::runtime::Session const& value) {
    auto const& limits = value.session_read_limits;
    return value.continuity.offline && value.continuity.prompt_overrides.system_prompt == std::optional<std::string>("continuity prompt override") &&
           value.continuity.diagnostics == expected_diagnostics && value.continuity.anchor_set == expected_anchor_set &&
           value.continuity.additional_writable_dirs == std::vector<std::filesystem::path>{additional_writable_dir} && limits.max_file_bytes == 8192 &&
           limits.max_line_bytes == 2048 && limits.max_entries == 32 && value.continuity.session_read_limits &&
           value.continuity.session_read_limits->max_entries == 32 && value.continuity.pin_model_override && value.continuity.default_model_override &&
           value.continuity.default_model_override->model_id == expected_pinned_model_id;
  };

  auto const source_session_id = session->store.session_id();
  auto named_source = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/name Old title"});
  expect(named_source && named_source->handled && !named_source->output.empty() && named_source->output[0] == "session name set: \"Old title\"",
         "slash new/resume command test names the source session for title-first lifecycle receipts");

  auto missing_resume = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/resume"});
  expect(missing_resume && missing_resume->handled && !missing_resume->output.empty() && missing_resume->output[0] == "usage: /resume <id>",
         "slash /resume without an id returns usage text");

  auto fresh = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/new Fresh session"});
  auto const fresh_session_id = session->store.session_id();
  auto fresh_metadata = ava::session::load_session_metadata(session->store);
  auto fresh_entries = session->store.load();
  auto const expected_fresh_receipt = "started session \"Fresh session\" · id " + fresh_session_id + "\nprevious session \"Old title\" · id " +
                                      source_session_id + "\nswitched to \"Fresh session\"";
  expect(fresh && fresh->handled && fresh->output.size() == 1 && fresh_session_id != source_session_id && fresh->output[0] == expected_fresh_receipt &&
             fresh->output[0].find("previous session " + source_session_id) == std::string::npos &&
             fresh->output[0].find("switched to " + fresh_session_id) == std::string::npos && fresh_metadata && fresh_metadata->name == "Fresh session" &&
             fresh_metadata->actor == "tui" && fresh_entries && fresh_entries->size() >= 2 && continuity_preserved(*session),
         "slash /new preserves configured runtime continuity, effective read limits, anchor authority, diagnostics identity, and model pin policy");

  auto resumed = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/resume " + source_session_id});
  expect(resumed && resumed->handled && !resumed->output.empty() && session->store.session_id() == source_session_id &&
             resumed->output[0].find("resumed session " + source_session_id) != std::string::npos && continuity_preserved(*session),
         "slash /resume preserves configured runtime continuity, effective read limits, anchor authority, diagnostics identity, and model pin policy");

  auto sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions Fresh"});
  expect(sessions && sessions->handled && !sessions->output.empty() && sessions->output[0].find("Fresh session") != std::string::npos &&
             sessions->output[0].find(fresh_session_id) != std::string::npos,
         "slash /sessions shows named sessions created through /new");

  auto unnamed = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/new"});
  auto const unnamed_session_id = session->store.session_id();
  auto const expected_unnamed_receipt = "started session \"Untitled session\" · id " + unnamed_session_id + "\nprevious session \"Old title\" · id " +
                                        source_session_id + "\nswitched to \"Untitled session\"";
  expect(unnamed && unnamed->handled && unnamed->output.size() == 1 && unnamed->output[0] == expected_unnamed_receipt,
         "slash /new uses the selector's Untitled session fallback in title-first lifecycle receipts");
}

void test_app_sessionless_new_and_resume_commands()
{
  auto const root = create_empty_root("app-sessionless-new-resume-commands");
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions options;
  options.continuity.workspace_dir = workspace;
  options.continuity.current_dir = workspace;
  options.continuity.paths = paths;
  options.request.sessionless = true;
  auto session = ava::app::open_runtime_session(options);
  expect(session && session->sessionless && session->store.is_ephemeral(), "sessionless slash navigation test opens an ephemeral runtime");
  if (!session)
    return;

  auto const original_session_id = session->store.session_id();
  auto const original_anchor_set = session->continuity.anchor_set;
  auto fresh = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/new Ephemeral session"});
  expect(fresh && fresh->handled && session->sessionless && session->store.is_ephemeral() && session->store.session_id() != original_session_id &&
             !std::filesystem::exists(session->store.session_path()) && session->continuity.anchor_set != original_anchor_set,
         "slash /new preserves sessionless navigation and rebinds authority around the new ephemeral runtime");

  auto resumed = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/resume " + original_session_id});
  expect(!resumed && resumed.error().message().find("no-session") != std::string::npos && session->sessionless && session->store.is_ephemeral(),
         "slash /resume from a sessionless runtime retains the no-session conflict instead of opening persistent state");
}

void test_app_sessionless_new_preserves_supplied_anchor()
{
  auto const root = create_empty_root("app-sessionless-new-supplied-anchor");
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::error_code temp_error;
  auto const temp_root = std::filesystem::temp_directory_path(temp_error);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  std::filesystem::create_directories(paths.ava_state_dir);
  expect(!temp_error, "sessionless supplied-anchor test resolves the temporary fixture root");
  if (temp_error)
    return;

  // The isolated fixture's temporary root covers both generated ephemeral
  // spill roots; config and state remain exact anchors.
  auto supplied_anchor_set = ava::core::AnchorSet::open({workspace, temp_root, paths.ava_config_dir, paths.ava_state_dir});
  expect(supplied_anchor_set.has_value(), "sessionless supplied-anchor test opens fixture authority before runtime creation");
  if (!supplied_anchor_set)
    return;

  ava::app::runtime::OpenOptions options;
  options.continuity.workspace_dir = workspace;
  options.continuity.current_dir = workspace;
  options.continuity.paths = paths;
  options.continuity.anchor_set = *supplied_anchor_set;
  options.request.sessionless = true;
  auto session = ava::app::open_runtime_session(options);
  expect(session && session->sessionless && session->continuity.anchor_set == *supplied_anchor_set && !session->continuity.anchor_set_is_generated,
         "sessionless supplied-anchor test retains caller authority with supplied provenance");
  if (!session)
    return;

  auto const original_session_id = session->store.session_id();
  auto fresh = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/new Supplied anchor session"});
  expect(fresh && fresh->handled && session->sessionless && session->store.is_ephemeral() && session->store.session_id() != original_session_id &&
             session->continuity.anchor_set == *supplied_anchor_set && !session->continuity.anchor_set_is_generated,
         "sessionless /new preserves supplied authority that covers the next ephemeral spill root");
}

void test_app_session_metadata_commands()
{
  auto const root = create_empty_root("app-session-metadata-commands");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.continuity.workspace_dir = workspace;
  open_options.continuity.current_dir = workspace;
  open_options.continuity.mode = ava::agent::Mode::Build;
  open_options.continuity.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "slash metadata command test opens runtime session");
  if (!session)
    return;

  auto missing_name = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/name"});
  expect(missing_name && missing_name->handled && !missing_name->session_tree_changed && !missing_name->output.empty() &&
             missing_name->output[0] == "usage: /name <name|--clear>",
         "slash /name without a name returns usage text");

  auto no_labels = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/labels"});
  expect(no_labels && no_labels->handled && !no_labels->session_tree_changed && !no_labels->output.empty() &&
             no_labels->output[0].find("session labels: <none>") != std::string::npos,
         "slash /labels without arguments reports current labels and usage");

  auto renamed = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/rename Auth follow-up"});
  auto metadata_after_name = ava::session::load_session_metadata(session->store);
  expect(renamed && renamed->handled && renamed->session_tree_changed && !renamed->output.empty() &&
             renamed->output[0].find("session name set: \"Auth follow-up\"") != std::string::npos && metadata_after_name &&
             metadata_after_name->name == "Auth follow-up" && metadata_after_name->actor == "tui",
         "slash /rename alias appends current-session name metadata");

  auto labeled = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/label auth bug"});
  auto metadata_after_labels = ava::session::load_session_metadata(session->store);
  expect(labeled && labeled->handled && labeled->session_tree_changed && !labeled->output.empty() &&
             labeled->output[0].find("session labels set: auth,bug") != std::string::npos && metadata_after_labels &&
             metadata_after_labels->name == "Auth follow-up" && metadata_after_labels->labels.size() == 2 && metadata_after_labels->labels[0] == "auth" &&
             metadata_after_labels->labels[1] == "bug" && metadata_after_labels->actor == "tui",
         "slash /label alias appends current-session label metadata without losing the session name");

  auto const slash_items = ava::app::command_catalog_slash_items(*session);
  auto has_session_completion = [](ava::tui::SlashCommandItem const* item, std::size_t argument_index, std::string_view value,
                                   std::string_view description_fragment, std::vector<std::string> previous_args = {}) {
    return item != nullptr && std::ranges::any_of(item->argument_completions, [&](auto const& completion) {
             return tui_test_support::slash_argument_completion_matches(completion, argument_index, value, previous_args) &&
                    completion.description.find(description_fragment) != std::string::npos;
           });
  };
  auto has_named_session_label = [&](ava::tui::SlashCommandItem const* item, std::size_t argument_index, std::vector<std::string> previous_args = {}) {
    return item != nullptr && std::ranges::any_of(item->argument_completions, [&](auto const& completion) {
             return tui_test_support::slash_argument_completion_matches(completion, argument_index, session->store.session_id(), previous_args) &&
                    completion.display_label == "Auth follow-up" && completion.description.find(session->store.session_id()) != std::string::npos;
           });
  };
  expect(has_named_session_label(tui_test_support::find_slash_command_item(slash_items, "/resume"), 0) &&
             has_session_completion(tui_test_support::find_slash_command_item(slash_items, "/resume"), 0, session->store.session_id(), "labels=auth, bug") &&
             has_named_session_label(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 0) &&
             has_session_completion(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 0, "rename", "Rename a session") &&
             has_session_completion(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 0, "labels", "Set or clear labels") &&
             has_session_completion(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 0, "archive", "Archive a session") &&
             has_session_completion(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 0, "unarchive", "Restore an archived session") &&
             has_named_session_label(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 1, {"rename"}) &&
             has_named_session_label(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 1, {"labels"}) &&
             has_session_completion(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 2, "--clear", "Clear labels",
                                    {"labels", session->store.session_id()}) &&
             has_named_session_label(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 1, {"archive"}) &&
             has_session_completion(tui_test_support::find_slash_command_item(slash_items, "/sessions"), 2, "--confirm", "Confirm archive",
                                    {"archive", session->store.session_id()}),
         "slash palette session completions expose session archive, rename, resume, and tree workflows");

  auto duplicate_labels = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/labels auth auth"});
  expect(!duplicate_labels && duplicate_labels.error().message() == "session labels must be unique",
         "slash /labels reuses backend metadata validation for duplicate labels");

  auto tree = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/tree auth"});
  expect(tree && tree->handled && !tree->output.empty() && tree->output[0].find("Auth follow-up") != std::string::npos &&
             tree->output[0].find("labels=auth,bug") != std::string::npos,
         "slash /tree exposes names and labels written through slash metadata commands");

  auto const active_session_id_before_selected_rename = session->store.session_id();
  auto renamed_selected =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions rename " + active_session_id_before_selected_rename + " Selector name"});
  auto metadata_after_selected_rename = ava::session::load_session_metadata(session->store);
  expect(renamed_selected && renamed_selected->handled && !renamed_selected->output.empty() &&
             renamed_selected->output[0].find("session " + active_session_id_before_selected_rename + " name set: \"Selector name\"") != std::string::npos &&
             metadata_after_selected_rename && metadata_after_selected_rename->name == "Selector name" &&
             session->store.session_id() == active_session_id_before_selected_rename,
         "slash /sessions rename appends name metadata to a selected session without switching runtime sessions");

  auto cleared_labels = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/labels --clear"});
  auto metadata_after_clear_labels = ava::session::load_session_metadata(session->store);
  expect(cleared_labels && cleared_labels->handled && !cleared_labels->output.empty() && cleared_labels->output[0] == "session labels cleared" &&
             metadata_after_clear_labels && metadata_after_clear_labels->labels.empty(),
         "slash /labels --clear appends empty label metadata");

  auto cleared_name = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/name --clear"});
  auto metadata_after_clear_name = ava::session::load_session_metadata(session->store);
  expect(cleared_name && cleared_name->handled && !cleared_name->output.empty() && cleared_name->output[0] == "session name cleared" &&
             metadata_after_clear_name && metadata_after_clear_name->name.empty(),
         "slash /name --clear appends empty name metadata");
}

}  // namespace ava::tests::app_runtime_tests
