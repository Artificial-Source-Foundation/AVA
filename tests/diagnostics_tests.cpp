#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/diagnostics/artifact_store.h"
#include "ava/diagnostics/records.h"
#include "ava/diagnostics/safe_failure.h"
#include "ava/agent/message_builder.h"
#include "ava/session/logical_projection.h"
#include "ava/session/portable_sanitization.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "debug.h"

namespace {

void test_closed_failure_serializers()
{
  constexpr std::array components{ava::diagnostics::ComponentClass::Mcp, ava::diagnostics::ComponentClass::Plugin};
  constexpr std::array categories{ava::diagnostics::FailureCategory::Configuration, ava::diagnostics::FailureCategory::Transport,
                                  ava::diagnostics::FailureCategory::Authorization, ava::diagnostics::FailureCategory::Protocol,
                                  ava::diagnostics::FailureCategory::Cancellation,  ava::diagnostics::FailureCategory::Internal};
  constexpr std::array codes{ava::diagnostics::FailureCode::InvalidRequest,  ava::diagnostics::FailureCode::IoFailure,
                             ava::diagnostics::FailureCode::NotFound,        ava::diagnostics::FailureCode::PermissionDenied,
                             ava::diagnostics::FailureCode::ExternalFailure, ava::diagnostics::FailureCode::Canceled,
                             ava::diagnostics::FailureCode::InternalFailure};
  constexpr std::array retryability{ava::diagnostics::Retryability::Never, ava::diagnostics::Retryability::AfterUserAction,
                                    ava::diagnostics::Retryability::Transient};
  constexpr std::array hints{ava::diagnostics::RecoveryHint::VerifyRequest, ava::diagnostics::RecoveryHint::VerifyConfiguration,
                             ava::diagnostics::RecoveryHint::VerifyPermissions, ava::diagnostics::RecoveryHint::RetryOperation,
                             ava::diagnostics::RecoveryHint::ContactSupport};

  for (auto const value : components) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure component enum has a fixed serializer");
  for (auto const value : categories) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure category enum has a fixed serializer");
  for (auto const value : codes) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure code enum has a fixed serializer");
  for (auto const value : retryability) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure retryability enum has a fixed serializer");
  for (auto const value : hints) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure recovery enum has a fixed serializer");

  auto const failure = ava::diagnostics::external_failure(ava::diagnostics::ComponentClass::Mcp);
  auto const json = ava::diagnostics::serialize_safe_failure_json(failure);
  auto const human = ava::diagnostics::serialize_safe_failure_human(failure);
  expect(ava::core::json::is_valid_object(json) && json.find("\"component\":\"mcp\"") != std::string::npos &&
             json.find("\"code\":\"external_failure\"") != std::string::npos,
         "SafeFailure JSON serializer emits only the stable closed representation");
  expect(human == "MCP integration operation failed [external_failure]. Verify the integration configuration before trying again.",
         "SafeFailure human serializer is stable and fixed");
}

void test_core_error_adapter_discards_raw_text()
{
  constexpr std::string_view canary = "CANARY_DIAGNOSTIC_SECRET_74a9";
  ava::core::Error error(ava::core::ErrorCategory::Io, std::string(canary));
  error.with_context("path", std::string(canary));
  auto const failure = ava::diagnostics::safe_failure_from_error(ava::diagnostics::ComponentClass::Plugin, error);
  auto const json = ava::diagnostics::serialize_safe_failure_json(failure);
  auto const human = ava::diagnostics::serialize_safe_failure_human(failure);
  expect(json.find(canary) == std::string::npos && human.find(canary) == std::string::npos,
         "SafeFailure adapter never serializes Error message or context text");
  expect(failure.code == ava::diagnostics::FailureCode::IoFailure && failure.retryability == ava::diagnostics::Retryability::Transient,
         "SafeFailure adapter classifies only the core ErrorCategory");
}

void test_external_tool_identity_boundary()
{
  auto const mcp = ava::diagnostics::external_tool_component("mcp_demo_echo");
  auto const plugin = ava::diagnostics::external_tool_component("plugin_demo_echo");
  expect(mcp == ava::diagnostics::ComponentClass::Mcp && plugin == ava::diagnostics::ComponentClass::Plugin,
         "reserved external-tool prefixes identify historical integration results");
  expect(!ava::diagnostics::external_tool_component("bash") && !ava::diagnostics::external_tool_component("xmcp_demo"),
         "unrelated built-in tool names are outside the fail-closed integration boundary");
}

void test_historical_external_failure_projection_is_safe()
{
  constexpr std::string_view canary = "CANARY_HISTORICAL_EXTERNAL_FAILURE_93c1";
  ava::session::SessionEntry call{.id = "entry_call",
                                  .parent_id = "",
                                  .type = ava::session::EntryType::ToolCall,
                                  .timestamp = "2026-01-01T00:00:00Z",
                                  .data_json = "{\"call_id\":\"call_external\",\"name\":\"mcp_demo_echo\",\"arguments\":\"{}\"}",
                                  .version = 3};
  ava::session::SessionEntry result{
      .id = "entry_result",
      .parent_id = "entry_call",
      .type = ava::session::EntryType::ToolResult,
      .timestamp = "2026-01-01T00:00:01Z",
      .data_json = "{\"call_id\":\"call_external\",\"name\":\"mcp_demo_echo\",\"success\":false,\"status\":\"error\",\"result\":\"" + std::string(canary) +
                   "\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_external\",\"tool\":\"mcp_demo_echo\",\"status\":\"error\","
                   "\"ok\":false,\"summary\":\"" +
                   std::string(canary) + "\",\"content_type\":\"text/plain\",\"content\":\"" + std::string(canary) +
                   "\",\"error\":{\"category\":\"tool\",\"message\":\"" + std::string(canary) + "\"},\"truncated\":false}}",
      .version = 3};
  std::vector<ava::session::SessionEntry> const entries{call, result};

  auto const replay = ava::agent::build_provider_messages_from_entries(entries);
  std::string replay_text;
  if (replay)
  {
    for (auto const& message : *replay)
    {
      replay_text += message.content;
      for (auto const& part : message.content_parts) replay_text += part.text;
    }
  }
  expect(replay && replay_text.find(canary) == std::string::npos && replay_text.find("external_failure") != std::string::npos,
         "provider replay replaces historical failed MCP content with SafeFailure");

  auto const public_projection = ava::session::project_logical_session_history(entries);
  auto const portable_projection = ava::session::project_portable_session_history(entries);
  auto const direct_portable = ava::session::sanitize_session_entry_for_portable_jsonl_export(result);
  expect(public_projection && public_projection->back().data_json.find(canary) == std::string::npos &&
             public_projection->back().data_json.find("external_failure") != std::string::npos,
         "public session projection removes historical failed MCP content");
  expect(portable_projection && portable_projection->back().data_json.find(canary) == std::string::npos &&
             portable_projection->back().data_json.find("external_failure") != std::string::npos && direct_portable.data_json.find(canary) == std::string::npos,
         "portable projection and direct portable sanitization remove historical failed MCP content");

  auto plugin_call = call;
  auto plugin_result = result;
  auto replace_tool_name = [](std::string& text) {
    constexpr std::string_view old_name = "mcp_demo_echo";
    constexpr std::string_view new_name = "plugin_demo_echo";
    for (auto offset = text.find(old_name); offset != std::string::npos; offset = text.find(old_name, offset + new_name.size()))
      text.replace(offset, old_name.size(), new_name);
  };
  replace_tool_name(plugin_call.data_json);
  replace_tool_name(plugin_result.data_json);
  std::vector<ava::session::SessionEntry> const plugin_entries{plugin_call, plugin_result};
  auto const plugin_replay = ava::agent::build_provider_messages_from_entries(plugin_entries);
  auto const plugin_public = ava::session::project_logical_session_history(plugin_entries);
  auto const plugin_portable = ava::session::project_portable_session_history(plugin_entries);
  std::string plugin_replay_text;
  if (plugin_replay)
  {
    for (auto const& message : *plugin_replay)
    {
      plugin_replay_text += message.content;
      for (auto const& part : message.content_parts) plugin_replay_text += part.text;
    }
  }
  expect(plugin_replay && plugin_public && plugin_portable && plugin_replay_text.find(canary) == std::string::npos &&
             plugin_public->back().data_json.find(canary) == std::string::npos && plugin_portable->back().data_json.find(canary) == std::string::npos,
         "provider replay and public/portable projections remove historical failed plugin content");

  auto successful = result;
  successful.data_json =
      "{\"call_id\":\"call_external\",\"name\":\"plugin_demo_echo\",\"success\":true,\"status\":\"success\",\"result\":\"" + std::string(canary) + "\"}";
  auto const preserved = ava::session::sanitize_session_entry_for_portable_jsonl_export(successful);
  expect(preserved.data_json.find(canary) != std::string::npos, "successful external-tool content remains unchanged by compatibility sanitization");
}

std::filesystem::path unique_diagnostic_root(std::string_view name)
{
  static std::atomic<unsigned long long> sequence{0};
  auto const root = temp_root() / (std::string(name) + "_" + std::to_string(static_cast<unsigned long long>(::getpid())) + "_" +
                                   std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "state");
  static_cast<void>(::chmod(root.c_str(), 0700));
  static_cast<void>(::chmod((root / "state").c_str(), 0700));
  return root;
}

ava::config::XdgPaths diagnostic_paths(std::filesystem::path const& root)
{
  ava::config::XdgPaths paths;
  paths.state_home = root / "state";
  paths.ava_state_dir = paths.state_home / "ava";
  return paths;
}

void write_test_file(std::filesystem::path const& path, std::string_view body)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(body.data(), static_cast<std::streamsize>(body.size()));
  file.close();
  static_cast<void>(::chmod(path.c_str(), 0600));
}

ava::diagnostics::DoctorReport sample_doctor_report()
{
  return ava::diagnostics::DoctorReport{.checks = {{.kind = ava::diagnostics::DoctorCheckKind::VersionPlatform,
                                                    .status = ava::diagnostics::DoctorStatus::Pass,
                                                    .code = ava::diagnostics::DoctorCode::Ready,
                                                    .items = 1}}};
}

void test_phase2_typed_serialization_and_strict_parsing()
{
  auto const report = sample_doctor_report();
  auto const report_json = ava::diagnostics::serialize_doctor_report_json(report);
  auto parsed_report = ava::diagnostics::parse_doctor_report_json(report_json);
  expect(parsed_report && parsed_report->checks.size() == 1 && !parsed_report->has_failures(), "doctor report has a strict typed round trip");
  auto duplicate_report = report_json;
  duplicate_report.insert(1, "\"schema_version\":1,");
  expect(!ava::diagnostics::parse_doctor_report_json(duplicate_report), "doctor report rejects duplicate object keys");

  ava::diagnostics::LastFailureRecord const failure{.recorded_at = 42,
                                                    .failure = {.component = ava::diagnostics::ComponentClass::Runtime,
                                                                .category = ava::diagnostics::FailureCategory::Internal,
                                                                .code = ava::diagnostics::FailureCode::InternalFailure,
                                                                .retryability = ava::diagnostics::Retryability::Never,
                                                                .recovery_hint = ava::diagnostics::RecoveryHint::ContactSupport},
                                                    .occurrences = 3};
  auto const failure_json = ava::diagnostics::serialize_last_failure_record(failure);
  auto parsed_failure = ava::diagnostics::parse_last_failure_record(failure_json);
  expect(parsed_failure && parsed_failure->failure.component == ava::diagnostics::ComponentClass::Runtime && parsed_failure->occurrences == 3,
         "last-failure record has a strict typed round trip");
  auto duplicate_failure = failure_json;
  duplicate_failure.insert(1, "\"recorded_at\":99,");
  expect(!ava::diagnostics::parse_last_failure_record(duplicate_failure), "last-failure record rejects duplicate keys");
  expect(!ava::diagnostics::parse_last_failure_record(std::string(65 * 1024, 'x')), "last-failure parser rejects oversized input");

  ava::diagnostics::TraceCounterSnapshot const counters{
      .captured_at = 84, .runtime_starts = 2, .provider_requests = 4, .provider_failures = 1, .session_failures = 3, .plugin_failures = 5, .mcp_failures = 6};
  auto parsed_counters = ava::diagnostics::parse_trace_counter_snapshot(ava::diagnostics::serialize_trace_counter_snapshot(counters));
  expect(parsed_counters && parsed_counters->provider_requests == 4 && parsed_counters->mcp_failures == 6,
         "trace-counter snapshot has a strict typed round trip");

  ava::diagnostics::SupportArtifact const support{.generated_at = 100,
                                                  .doctor = report,
                                                  .trace = {.state = ava::diagnostics::StoredRecordState::Present, .record = counters},
                                                  .last_failure = {.state = ava::diagnostics::StoredRecordState::Present, .record = failure}};
  auto const support_json = ava::diagnostics::serialize_support_artifact(support);
  expect(ava::core::json::is_valid_object(support_json) && support_json.find("\"trace\":") != std::string::npos &&
             support_json.find("\"last_failure\":") != std::string::npos && support_json.find("CANARY") == std::string::npos,
         "support serializer emits the fixed typed schema only");
}

void test_private_artifact_storage_and_atomic_replacement()
{
  auto const root = unique_diagnostic_root("diagnostic_store_atomic");
  auto const paths = diagnostic_paths(root);
  ava::diagnostics::LastFailureRecord first{
      .recorded_at = 1, .failure = ava::diagnostics::external_failure(ava::diagnostics::ComponentClass::Provider), .occurrences = 1};
  ava::diagnostics::LastFailureRecord second{
      .recorded_at = 2, .failure = ava::diagnostics::canceled_failure(ava::diagnostics::ComponentClass::Runtime), .occurrences = 2};
  expect(ava::diagnostics::write_last_failure_record(paths, first) == ava::diagnostics::ArtifactWriteStatus::Success,
         "private storage creates the diagnostic tree");
  expect(ava::diagnostics::write_last_failure_record(paths, second) == ava::diagnostics::ArtifactWriteStatus::Success,
         "last-failure update atomically replaces a valid record");
  auto const read = ava::diagnostics::read_last_failure_record(paths);
  expect(read.state == ava::diagnostics::StoredRecordState::Present && read.record && read.record->recorded_at == 2 && read.record->occurrences == 2,
         "last-failure reader observes the complete replacement");
  ava::diagnostics::TraceCounterSnapshot const counters{.captured_at = 3, .runtime_starts = 4, .provider_requests = 5};
  expect(ava::diagnostics::write_trace_counter_snapshot(paths, counters) == ava::diagnostics::ArtifactWriteStatus::Success,
         "trace counters use the private typed storage boundary");
  auto const counters_read = ava::diagnostics::read_trace_counter_snapshot(paths);
  expect(counters_read.state == ava::diagnostics::StoredRecordState::Present && counters_read.record && counters_read.record->runtime_starts == 4 &&
             counters_read.record->provider_requests == 5,
         "trace-counter reader observes a complete typed record");

  struct stat directory_metadata{};
  struct stat file_metadata{};
  auto const diagnostics_dir = paths.ava_state_dir / "diagnostics";
  auto const record_file = diagnostics_dir / "last-failure-v1.json";
  expect(::stat(diagnostics_dir.c_str(), &directory_metadata) == 0 && (directory_metadata.st_mode & 0777) == 0700,
         "diagnostic directories are exact mode 0700");
  expect(::stat(record_file.c_str(), &file_metadata) == 0 && (file_metadata.st_mode & 0777) == 0600 && file_metadata.st_nlink == 1 &&
             file_metadata.st_uid == ::geteuid() && file_metadata.st_gid == ::getegid(),
         "diagnostic files are owner-owned, exact mode 0600, and singly linked");
  bool temporary_found = false;
  for (auto const& entry : std::filesystem::directory_iterator(diagnostics_dir))
    temporary_found = temporary_found || entry.path().filename().string().starts_with(".diagnostic-tmp-");
  expect(!temporary_found, "successful atomic replacement leaves no temporary artifact");
  std::filesystem::remove_all(root);
}

void test_private_artifact_reader_rejects_unsafe_and_malformed_files()
{
  auto const root = unique_diagnostic_root("diagnostic_store_reject");
  auto const paths = diagnostic_paths(root);
  ava::diagnostics::LastFailureRecord const record{.recorded_at = 1, .failure = ava::diagnostics::external_failure(ava::diagnostics::ComponentClass::Storage)};
  expect(ava::diagnostics::write_last_failure_record(paths, record) == ava::diagnostics::ArtifactWriteStatus::Success,
         "unsafe-record fixtures begin with a valid private record");
  auto const file = paths.ava_state_dir / "diagnostics" / "last-failure-v1.json";
  auto const sibling = paths.ava_state_dir / "diagnostics" / "sibling";

  static_cast<void>(::chmod(file.c_str(), 0644));
  expect(ava::diagnostics::read_last_failure_record(paths).state == ava::diagnostics::StoredRecordState::Unsafe,
         "diagnostic reader rejects wrong file permissions");
  std::filesystem::remove(file);
  write_test_file(sibling, "{}\n");
  std::filesystem::create_symlink(sibling.filename(), file);
  expect(ava::diagnostics::read_last_failure_record(paths).state == ava::diagnostics::StoredRecordState::Unsafe, "diagnostic reader rejects symbolic links");
  std::filesystem::remove(file);
  expect(::mkfifo(file.c_str(), 0600) == 0, "FIFO fixture is created");
  expect(ava::diagnostics::read_last_failure_record(paths).state == ava::diagnostics::StoredRecordState::Unsafe,
         "diagnostic reader rejects FIFOs without blocking");
  std::filesystem::remove(file);
  write_test_file(file, ava::diagnostics::serialize_last_failure_record(record));
  auto const hardlink = paths.ava_state_dir / "diagnostics" / "hardlink";
  std::filesystem::create_hard_link(file, hardlink);
  expect(ava::diagnostics::read_last_failure_record(paths).state == ava::diagnostics::StoredRecordState::Unsafe,
         "diagnostic reader rejects multiply linked files");
  std::filesystem::remove(hardlink);

  write_test_file(file, "{\"schema_version\":1,\"schema_version\":1}");
  expect(ava::diagnostics::read_last_failure_record(paths).state == ava::diagnostics::StoredRecordState::Malformed,
         "diagnostic reader reports duplicate-key records as malformed");
  write_test_file(file, std::string(65 * 1024, 'x'));
  expect(ava::diagnostics::read_last_failure_record(paths).state == ava::diagnostics::StoredRecordState::Malformed,
         "diagnostic reader reports oversized records as malformed");

  std::filesystem::remove(file);
  expect(ava::diagnostics::read_last_failure_record(paths).state == ava::diagnostics::StoredRecordState::Absent,
         "missing optional diagnostic record is absent");
  std::filesystem::remove_all(root);
}

void test_support_publication_is_unique_private_and_concurrent()
{
  auto const root = unique_diagnostic_root("diagnostic_support_publish");
  auto const paths = diagnostic_paths(root);
  ava::diagnostics::SupportArtifact const artifact{.generated_at = 123, .doctor = sample_doctor_report(), .trace = {}, .last_failure = {}};
  constexpr std::size_t publications = 8;
  std::array<ava::diagnostics::ArtifactWriteStatus, publications> statuses{};
  std::vector<std::thread> threads;
  for (std::size_t index = 0; index < publications; ++index)
  {
    threads.emplace_back([&, index] { statuses[index] = ava::diagnostics::publish_support_artifact(paths, artifact); });
  }
  for (auto& thread : threads) thread.join();
  expect(std::ranges::all_of(statuses, [](auto status) { return status == ava::diagnostics::ArtifactWriteStatus::Success; }),
         "concurrent support publications all complete");

  auto const support_dir = paths.ava_state_dir / "support";
  std::size_t published = 0;
  bool safe = true;
  bool partial = false;
  for (auto const& entry : std::filesystem::directory_iterator(support_dir))
  {
    auto const name = entry.path().filename().string();
    partial = partial || name.starts_with(".support-tmp-");
    if (!name.starts_with("ava-support-v1-") || !name.ends_with(".json"))
      continue;
    ++published;
    struct stat metadata{};
    safe = safe && ::stat(entry.path().c_str(), &metadata) == 0 && S_ISREG(metadata.st_mode) && (metadata.st_mode & 0777) == 0600 && metadata.st_nlink == 1 &&
           metadata.st_uid == ::geteuid() && metadata.st_gid == ::getegid();
    std::ifstream input(entry.path(), std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    safe = safe && ava::core::json::is_valid_object(body);
  }
  expect(published == publications && safe, "support publication is unique, valid, owner-only, and no-replace");
  expect(!partial, "support publication never leaves a partial published artifact");

  static_cast<void>(::chmod(support_dir.c_str(), 0755));
  expect(ava::diagnostics::publish_support_artifact(paths, artifact) == ava::diagnostics::ArtifactWriteStatus::UnsafeStorage,
         "support publication rejects an unsafe existing directory");
  std::size_t after_failed_publication = 0;
  for (auto const& entry : std::filesystem::directory_iterator(support_dir))
    after_failed_publication += entry.path().filename().string().starts_with("ava-support-v1-") ? 1U : 0U;
  expect(after_failed_publication == publications, "failed support publication exposes no partial or extra artifact");
  std::filesystem::remove_all(root);
}

void test_private_artifact_storage_rejects_unsafe_ancestors()
{
  auto const root = unique_diagnostic_root("diagnostic_store_ancestor");
  auto const paths = diagnostic_paths(root);
  std::filesystem::create_directories(root / "replacement");
  static_cast<void>(::chmod((root / "replacement").c_str(), 0700));
  std::filesystem::create_directory_symlink(root / "replacement", paths.ava_state_dir);
  ava::diagnostics::TraceCounterSnapshot const snapshot{.captured_at = 1};
  expect(ava::diagnostics::write_trace_counter_snapshot(paths, snapshot) == ava::diagnostics::ArtifactWriteStatus::UnsafeStorage,
         "artifact writer rejects a symlinked AVA state root");
  std::filesystem::remove(paths.ava_state_dir);
  std::filesystem::create_directory(paths.ava_state_dir);
  static_cast<void>(::chmod(paths.ava_state_dir.c_str(), 0755));
  expect(ava::diagnostics::write_trace_counter_snapshot(paths, snapshot) == ava::diagnostics::ArtifactWriteStatus::UnsafeStorage,
         "artifact writer rejects wrong private-directory permissions");
  std::filesystem::remove_all(root);
}

}  // namespace

void run_diagnostics_tests()
{
  test_closed_failure_serializers();
  test_core_error_adapter_discards_raw_text();
  test_external_tool_identity_boundary();
  test_historical_external_failure_projection_is_safe();
  test_phase2_typed_serialization_and_strict_parsing();
  test_private_artifact_storage_and_atomic_replacement();
  test_private_artifact_reader_rejects_unsafe_and_malformed_files();
  test_support_publication_is_unique_private_and_concurrent();
  test_private_artifact_storage_rejects_unsafe_ancestors();
}
