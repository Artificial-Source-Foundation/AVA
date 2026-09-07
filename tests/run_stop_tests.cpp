#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/RunOptions.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_compaction.h"
#include "ava/agent/message_builder.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/logical_projection.h"
#include "ava/session/record.h"
#include "ava/session/run_stop.h"
#include "ava/session/session_branch.h"
#include "ava/session/stats.h"
#include "ava/session/validation.h"
#include "ava/provider/openai_provider.h"
#include "ava/core/json.h"

#include <algorithm>
#include <fstream>
#include <set>

namespace {

using ava::session::EntryType;
using ava::session::SessionEntry;

std::size_t count_type(std::vector<SessionEntry> const& entries, EntryType type)
{
  return static_cast<std::size_t>(std::ranges::count_if(entries, [type](auto const& entry) { return entry.type == type; }));
}

void schema_tests()
{
  auto stop = ava::session::make_run_stop_entry({.reason = "Work is incomplete.", .round_count = 10});
  expect(stop.has_value(), "RunStop producer creates a valid closed payload");
  if (!stop)
    return;
  auto wire = ava::session::serialize_session_entry_line(*stop);
  auto parsed = wire ? ava::session::parse_session_entry_line(*wire, "synthetic.jsonl") : ava::core::Result<SessionEntry>(std::unexpected(wire.error()));
  expect(parsed && parsed->type == EntryType::RunStop && parsed->data_json == stop->data_json, "RunStop envelope round-trips");
  auto original = stop->data_json;
  auto replace = [&](std::string_view from, std::string_view to) {
    auto json = original;
    json.replace(json.find(from), from.size(), to);
    return json;
  };
  std::vector<std::string> bad = {"",
                                  "{}",
                                  "[]",
                                  "null",
                                  replace("\"schema_version\":1,", ""),
                                  replace("\"schema_version\":1", "\"schema_version\":2"),
                                  replace("\"schema_version\":1", "\"schema_version\":\"1\""),
                                  replace("\"classification\":\"max_turn_requests\",", ""),
                                  replace("max_turn_requests", "cancel_requested"),
                                  replace("\"status\":\"paused\",", ""),
                                  replace("\"paused\"", "\"completed\""),
                                  replace("\"reason\":\"Work is incomplete.\",", ""),
                                  replace("Work is incomplete.", ""),
                                  replace("Work is incomplete.", std::string(1025, 'x')),
                                  replace("Work is incomplete.", "bad\\nreason"),
                                  replace(",\"round_count\":10", ""),
                                  replace("\"round_count\":10", "\"round_count\":0"),
                                  replace("\"round_count\":10", "\"round_count\":-1"),
                                  replace("\"round_count\":10", "\"round_count\":\"10\""),
                                  replace("\"round_count\":10", "\"round_count\":10.0"),
                                  replace("\"round_count\":10", "\"round_count\":1e1"),
                                  replace("\"round_count\":10", "\"round_count\":true"),
                                  replace("\"round_count\":10", "\"round_count\":9223372036854775808"),
                                  replace("\"round_count\":10", "\"round_count\":10,\"round_count\":11"),
                                  replace("\"round_count\":10", "\"round_count\":10,\"executor\":{}"),
                                  replace("round_count", "round_\\u0063ount")};
  for (auto const& json : bad)
  {
    auto invalid = *stop;
    invalid.data_json = json;
    auto report = ava::session::validate_session_replay({invalid});
    expect(!report.ok() &&
               std::ranges::any_of(report.issues, [](auto const& issue) { return issue.kind == ava::session::SessionReplayIssueKind::InvalidRunStopEntry; }),
           "malformed RunStop fails with its strict validation category");
    expect(!ava::session::serialize_session_entry_line(invalid) && !ava::session::project_ordered_public_session_history({invalid}),
           "malformed RunStop cannot be serialized or silently projected");
  }
  auto misplaced = *stop;
  misplaced.type = EntryType::SessionMetadata;
  auto invalid_metadata = ava::session::validate_session_replay({misplaced});
  expect(!invalid_metadata.ok() && invalid_metadata.issues.front().kind == ava::session::SessionReplayIssueKind::InvalidSessionMetadataEntry,
         "RunStop under SessionMetadata still fails unchanged tree validation");
  auto orphan = *stop;
  orphan.parent_id = "missing_parent";
  auto ancestry = ava::session::validate_session_replay({orphan});
  expect(!ancestry.ok() && ancestry.issues.front().kind == ava::session::SessionReplayIssueKind::UnknownParentId, "RunStop obeys generic envelope ancestry");
  auto duplicate = *stop;
  expect(!ava::session::validate_session_replay({*stop, duplicate}).ok(), "RunStop obeys generic unique entry identity");
  auto invalid_parent_wire = *wire;
  invalid_parent_wire.replace(invalid_parent_wire.find("\"parent_id\":\"\""), 14, "\"parent_id\":\"../bad\"");
  expect(!ava::session::parse_session_entry_line(invalid_parent_wire, "synthetic.jsonl"), "RunStop rejects unsafe envelope parent syntax");

  auto visible = ava::session::project_ordered_public_session_history({*stop});
  auto messages = ava::agent::build_provider_messages_from_entries({*stop});
  auto before = ava::agent::prepared_context_usage({}, "system");
  auto after = ava::agent::prepared_context_usage({*stop}, "system");
  expect(visible && visible->size() == 1 && visible->front().type == EntryType::RunStop, "human history retains typed RunStop chronology");
  expect(messages && messages->empty() && before && after && before->tokens == after->tokens,
         "RunStop is not a provider message and alone adds no context tokens");
  auto markdown = ava::session::format_session_markdown_checked({*stop});
  auto html = ava::session::format_session_html_checked({*stop});
  auto archive = ava::session::format_session_portable_jsonl_checked({*stop});
  expect(markdown && markdown->contains("Run Stop (max_turn_requests)") && markdown->contains("Continue to resume.") && html &&
             html->contains("max_turn_requests") && archive && archive->contains("\"type\":\"run_stop\""),
         "exports truthfully expose runtime evidence and derive Continue guidance");
  auto stats = ava::session::compute_session_stats({*stop});
  expect(stats && stats->counts.run_stop == 1 && stats->counts.cancel == 0 && stats->counts.error == 0 && !stats->total_tokens,
         "RunStop is neither a cancellation/error nor provider usage");
}

void persisted_cycles()
{
  auto root = create_empty_root("run-stop-cycles");
  auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::ofstream(workspace / "note.txt") << std::string(6000, 'x');
  ava::app::runtime::OpenContext open;
  open.workspace_dir = workspace;
  open.current_dir = workspace;
  open.paths = ava::tests::app_test_paths(root);
  ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::app::runtime::RunOptions options;
  options.access_token = "synthetic-token";
  std::string id;
  std::vector<SessionEntry> previous;
  std::size_t initial_tokens = 0;
  std::size_t prior_tokens = 0;
  for (int cycle = 0; cycle < 2; ++cycle)
  {
    auto session = id.empty() ? ava::app::runtime::Session::open(open) : ava::app::runtime::Session::open(open, {.requested_session_id = id});
    expect(session.has_value(), "bounded session opens/reopens between cycles");
    if (!session)
      return;
    auto loaded = ava::app::runtime::session_ts::rat(*session)->store.load();
    auto model = ava::app::runtime::session_ts::rat(*session)->model();
    auto system = ava::app::runtime::session_ts::rat(*session)->system_prompt();
    auto current = ava::app::runtime::context_usage(*loaded, system, model);
    expect(current && current->estimated, "initial/restored active context is explicitly estimated");
    if (cycle == 0)
      initial_tokens = current->tokens;
    else
    {
      expect(current->tokens == prior_tokens && count_type(*loaded, EntryType::RunStop) == 1,
             "reopen reconstructs identical active context and retains the first stop");
      expect(loaded->size() == previous.size(), "reopen neither drops nor duplicates retained entries");
    }
    std::vector<ava::http::HttpResponse> responses;
    for (int round = 0; round < 10; ++round)
      responses.push_back(ava::tests::sse_response(ava::tests::read_file_call_sse("note.txt", "cycle-" + std::to_string(cycle) + "-" + std::to_string(round))));
    responses.push_back(ava::tests::sse_response(ava::tests::final_text_sse("must not request this turn")));
    ava::tests::FakeTransport transport(std::move(responses));
    auto result = ava::app::run_prompt(*session, cycle == 0 ? "Read ten times" : "Continue", provider, transport, options);
    expect(result && result->outcome == ava::core::RuntimeTerminalOutcome::MaxTurnRequests && result->tool_iterations == 10 && result->tool_calls == 10 &&
               transport.requests().size() == 10,
           "each new executor stops at tenth completed round without an eleventh request");
    auto entries = ava::app::runtime::session_ts::rat(*session)->store.load();
    expect(entries && ava::session::validate_session_replay(*entries).ok(), "actual persisted bounded history passes full replay validation");
    if (!entries)
      return;
    expect(count_type(*entries, EntryType::RunStop) == static_cast<std::size_t>(cycle + 1) &&
               count_type(*entries, EntryType::ToolResult) == static_cast<std::size_t>((cycle + 1) * 10),
           "one stop per boundary and exactly one result per performed tool");
    std::set<std::string> calls;
    for (auto const& entry : *entries)
    {
      if (entry.type == EntryType::ToolResult)
        expect(calls.insert(*ava::core::json::string_field(entry.data_json, "call_id")).second, "no prior tool result is duplicated by Continue");
      if (entry.type == EntryType::RunStop)
      {
        auto stop = ava::session::parse_run_stop(entry);
        expect(stop && stop->round_count == 10 && stop->reason == "Paused after 10 tool rounds. Work is incomplete." &&
                   ava::core::json::string_field(entry.data_json, "classification") == "max_turn_requests" &&
                   ava::core::json::string_field(entry.data_json, "status") == "paused",
               "durable classification, status, reason and completed count survive");
      }
      expect(entry.type != EntryType::SessionMetadata || !entry.data_json.contains("runtime-stop"), "no experimental metadata receipt persists");
    }
    if (cycle != 0)
      expect(transport.requests().front().body.contains("cycle-0-9") && transport.requests().front().body.contains("Continue"),
             "Continue is a new provider request over retained prior results");
    auto usage = ava::app::runtime::context_usage(*entries, system, model);
    auto without_stop = *entries;
    std::erase_if(without_stop, [](auto const& e) { return e.type == EntryType::RunStop; });
    auto control = ava::app::runtime::context_usage(without_stop, system, model);
    expect(usage && control && usage->tokens == control->tokens && usage->tokens > initial_tokens && (cycle == 0 || usage->tokens > prior_tokens),
           "retained tool results/multiple turns grow active context; RunStop itself does not");
    prior_tokens = usage->tokens;
    id = ava::app::runtime::session_ts::rat(*session)->store.session_id();
    previous = *entries;
  }
  auto final = ava::app::runtime::Session::open(open, {.requested_session_id = id});
  expect(final.has_value(), "second stop session reopens");
  if (!final)
    return;
  ava::tests::FakeTransport completed({ava::tests::sse_response(ava::tests::final_text_sse("done without duplicate work"))});
  auto resumed = ava::app::run_prompt(*final, "Continue", provider, completed, options);
  auto entries = ava::app::runtime::session_ts::rat(*final)->store.load();
  expect(resumed && resumed->tool_calls == 0 && resumed->tool_iterations == 0 && completed.requests().size() == 1 &&
             count_type(*entries, EntryType::ToolResult) == 20 && count_type(*entries, EntryType::RunStop) == 2 &&
             ava::session::validate_session_replay(*entries).ok(),
         "below-boundary Continue completes normally without another stop or duplicated work");
  auto fresh = ava::app::runtime::Session::open(open);
  expect(fresh.has_value(), "new session opens independently");
  if (fresh)
  {
    auto read = ava::app::runtime::session_ts::rat(*fresh);
    auto fresh_entries = read->store.load();
    auto usage = ava::app::runtime::context_usage(*fresh_entries, read->system_prompt(), read->model());
    expect(usage && usage->tokens == initial_tokens && count_type(*fresh_entries, EntryType::RunStop) == 0,
           "new-session active context resets without inheriting old stops");
  }
  {
    auto read = ava::app::runtime::session_ts::rat(*final);
    ava::session::ManualCompactionRequest request;
    request.summary = "Two bounded read batches completed; preserve their results.";
    request.config = ava::session::default_compaction_config();
    request.estimated_tokens = prior_tokens;
    auto compact = ava::session::make_manual_compaction_entry(std::move(request));
    expect(compact.has_value(), "valid compaction marker builds");
    if (!compact)
      return;
    auto appended = read->append_target()->append(std::move(*compact));
    expect(appended.has_value(), "compaction appends without rewriting RunStop history");
    auto compacted = read->store.load();
    auto usage = ava::app::runtime::context_usage(*compacted, read->system_prompt(), read->model());
    expect(compacted && ava::session::validate_session_replay(*compacted).ok() && count_type(*compacted, EntryType::RunStop) == 2 && usage &&
               usage->tokens < prior_tokens,
           "compaction reduces active context while both durable stops remain valid");
    for (auto mode : {ava::session::SessionBranchMode::Fork, ava::session::SessionBranchMode::Clone})
    {
      ava::session::SessionBranchOptions branch_options;
      branch_options.workspace_dir = workspace;
      branch_options.root_dir = open.paths.sessions_dir;
      branch_options.source_session_id = id;
      branch_options.source_lease = &read->lease();
      branch_options.mode = mode;
      if (mode == ava::session::SessionBranchMode::Fork)
        branch_options.branch_from_entry_id = compacted->back().id;
      auto branch = ava::session::create_session_branch(std::move(branch_options));
      expect(branch.has_value(), "bounded-stop history can be forked/cloned through validated-copy path");
      if (branch)
      {
        auto copied = branch->store.load(branch->lease);
        expect(copied && count_type(*copied, EntryType::RunStop) == 2 && ava::session::validate_session_replay(*copied).ok(),
               "fork/clone preserves both typed stops with valid tree provenance");
      }
    }
  }
}

void non_boundary_outcomes()
{
  auto root = create_empty_root("run-stop-distinction");
  auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto outside = root / "outside.txt";
  std::ofstream(outside) << "synthetic outside content";
  ava::app::runtime::OpenContext open;
  open.workspace_dir = workspace;
  open.current_dir = workspace;
  open.paths = ava::tests::app_test_paths(root);
  ava::provider::OpenAIProvider provider("https://api.example.test");
  for (int scenario = 0; scenario < 3; ++scenario)
  {
    auto session = ava::app::runtime::Session::open(open);
    expect(session.has_value(), "non-boundary fixture opens");
    if (!session)
      return;
    ava::app::runtime::RunOptions options;
    options.access_token = "synthetic-token";
    int prompts = 0;
    if (scenario != 2)
      options.permission_resolver = [&](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        auto waiting = ava::app::runtime::session_ts::rat(*session)->store.load();
        expect(waiting && count_type(*waiting, EntryType::RunStop) == 0, "approval-required callback does not itself persist a bounded stop");
        return scenario == 0 ? ava::permissions::PermissionResolution::Allow : ava::permissions::PermissionResolution::Deny;
      };
    auto path = scenario == 2 ? workspace / "does-not-exist.txt" : outside;
    ava::tests::FakeTransport transport({ava::tests::sse_response(ava::tests::read_file_call_sse(path.string(), "distinct-call")),
                                         ava::tests::sse_response(ava::tests::final_text_sse("handled one result"))});
    auto result = ava::app::run_prompt(*session, "Read once", provider, transport, options);
    auto entries = ava::app::runtime::session_ts::rat(*session)->store.load();
    expect(result && result->tool_iterations == 1 && result->outcome == ava::core::RuntimeTerminalOutcome::Completed && transport.requests().size() == 2 &&
               entries && count_type(*entries, EntryType::RunStop) == 0 && (scenario == 2 || prompts == 1),
           "approval, denial and ordinary error remain distinct from a ten-round bound");
    if (result && scenario != 0)
      expect(result->tool_timeline.size() == 1 && result->tool_timeline.front().status != ava::agent::ToolTimelineStatus::Success,
             "denial/error distinction fixture really produces a non-success tool result");
  }
}

}  // namespace

namespace ava::tests::app_runtime_tests {

void test_run_stop_schema_and_persistence()
{
  schema_tests();
  persisted_cycles();
  non_boundary_outcomes();
}

}  // namespace ava::tests::app_runtime_tests
