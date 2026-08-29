#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/app/command_catalog.h"
#include "ava/tui/command_output.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/event_state.h"
#include "ava/core/json.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

namespace {

using TranscriptSemantic = std::tuple<std::string, std::string, std::string>;

std::vector<TranscriptSemantic> transcript_semantics(std::vector<ava::tui::TranscriptItem> const& transcript)
{
  std::vector<TranscriptSemantic> semantics;
  semantics.reserve(transcript.size());
  for (auto const& item : transcript)
    semantics.emplace_back(item.label, item.text, item.tool ? item.tool->name : std::string{});
  return semantics;
}

bool frame_contains(std::vector<std::string> const& frame, std::string_view needle)
{
  return std::ranges::any_of(frame, [&](std::string const& line) { return strip_sgr(line).find(needle) != std::string::npos; });
}

void test_command_output_rendering_and_input()
{
  std::vector<std::string> blocks;
  for (std::size_t index = 0; index < 80; ++index)
    blocks.push_back("line " + std::to_string(index));
  auto view = ava::tui::make_command_output_view("/bash super-secret-argument", blocks);
  auto const frame = ava::tui::render_command_output(view, 60, 10);
  expect(frame.size() == 10 && frame_contains(frame, "Command /bash") && !frame_contains(frame, "super-secret-argument") &&
             frame_contains(frame, "Enter close") && ava::tui::command_output_max_scroll_offset(view, 60, 10) > 0,
         "command output renders bounded token-only chrome with scroll controls");

  auto end = ava::tui::handle_command_output_input(view, ava::tui::InputEvent{.key = ava::tui::Key::End}, 60, 10);
  expect(end.action == ava::tui::CommandOutputInputAction::Redraw && end.scroll_offset == ava::tui::command_output_max_scroll_offset(view, 60, 10),
         "command output End moves to the final wrapped row");
  view.scroll_offset = end.scroll_offset;
  auto page_up = ava::tui::handle_command_output_input(view, ava::tui::InputEvent{.key = ava::tui::Key::PageUp}, 60, 10);
  auto arrow_up = ava::tui::handle_command_output_input(view, ava::tui::InputEvent{.key = ava::tui::Key::ArrowUp}, 60, 10);
  auto wheel_up = ava::tui::handle_command_output_input(view, ava::tui::InputEvent{.key = ava::tui::Key::MouseWheelUp}, 60, 10);
  auto home = ava::tui::handle_command_output_input(view, ava::tui::InputEvent{.key = ava::tui::Key::Home}, 60, 10);
  expect(page_up.scroll_offset < end.scroll_offset && arrow_up.scroll_offset + 1 == end.scroll_offset && wheel_up.scroll_offset == arrow_up.scroll_offset &&
             home.scroll_offset == 0,
         "command output arrows, PageUp, Home, and mouse wheel share bounded scrolling");

  for (auto const key : {ava::tui::Key::Escape, ava::tui::Key::CtrlC, ava::tui::Key::Enter})
  {
    expect(ava::tui::handle_command_output_input(view, ava::tui::InputEvent{.key = key}, 60, 10).action == ava::tui::CommandOutputInputAction::Dismiss,
           "command output supports each required dismissal key");
  }

  auto sanitized = ava::tui::make_command_output_view("!! printf secret", {std::string("safe\x1b[31m\nnext\trow")});
  auto const sanitized_frame = ava::tui::render_command_output(sanitized, 40, 8);
  expect(sanitized.title_token == "!!" && sanitized.blocks.front().find('\x1b') == std::string::npos && sanitized.blocks.front() == "safe?[31m\nnext  row" &&
             frame_contains(sanitized_frame, "safe?[31m") && frame_contains(sanitized_frame, "next  row"),
         "command output sanitizes terminal controls while preserving lines and shell-helper token privacy");

  std::vector<std::string> excessive_blocks(40, "block");
  auto excessive = ava::tui::make_command_output_view("/report", excessive_blocks);
  auto excessive_bytes = ava::tui::make_command_output_view("/report", {std::string(300 * 1024, 'x')});
  expect(excessive.truncated && excessive.blocks.size() <= 32 && excessive_bytes.truncated && excessive_bytes.blocks.front().size() <= 256 * 1024,
         "command output bounds block count and retained bytes before presentation");

  constexpr auto output_byte_limit = std::size_t{256 * 1024};
  auto boundary_split = std::string(output_byte_limit - 1, 'x') + "€";
  auto multibyte_boundary = ava::tui::make_command_output_view("/report", {boundary_split});
  auto boundary_fit = ava::tui::make_command_output_view("/report", {std::string(output_byte_limit - 3, 'x') + "€"});
  auto split_title = ava::tui::command_output_title_token("/" + std::string(62, 'x') + "€ private-argument");
  auto fitting_title = ava::tui::command_output_title_token("/" + std::string(60, 'x') + "€ private-argument");
  expect(multibyte_boundary.truncated && multibyte_boundary.blocks.front().size() == output_byte_limit - 1 &&
             ava::core::json::is_valid_utf8(multibyte_boundary.blocks.front()) && boundary_fit.blocks.front().size() == output_byte_limit &&
             boundary_fit.blocks.front().ends_with("€") && ava::core::json::is_valid_utf8(boundary_fit.blocks.front()) && split_title.size() == 63 &&
             ava::core::json::is_valid_utf8(split_title) && fitting_title.size() == 64 && fitting_title.ends_with("€") &&
             ava::core::json::is_valid_utf8(fitting_title),
         "command output byte bounding keeps exact fitting multibyte code points and truncates before split UTF-8 output/title sequences");

  for (auto const& [width, height] : {std::pair<std::size_t, std::size_t>{20, 5}, {40, 8}, {160, 48}})
  {
    auto const geometry = ava::tui::command_output_geometry(width, height);
    auto const resized = ava::tui::render_command_output(view, geometry.width, geometry.height);
    expect(resized.size() == geometry.height && std::ranges::all_of(resized, [&](std::string const& line) { return visible_columns(line) <= geometry.width; }),
           "command output remains width- and height-bounded across terminal sizes");
  }
}

void test_command_output_modal_precedence_and_hits()
{
  ava::tui::ComposerSnapshot snapshot;
  snapshot.mode = "build";
  snapshot.provider = "openai";
  snapshot.model = "gpt-5.5";
  snapshot.session_id = "session_test";
  snapshot.input = "/help";
  snapshot.input_cursor = snapshot.input.size();
  snapshot.transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "identifiable transcript"}};
  snapshot.command_output = ava::tui::make_command_output_view("/help secret", {"local help output"});
  snapshot.select_list = ava::tui::SelectListView{};
  snapshot.select_list->title = "Must stay behind command output";
  snapshot.width = 80;
  snapshot.height = 16;
  auto const command_frame = ava::tui::render_composer(snapshot);
  expect(frame_contains(command_frame, "Command /help") && frame_contains(command_frame, "local help output") &&
             !frame_contains(command_frame, "Must stay behind command output"),
         "command output takes precedence over ordinary selector and transcript presentation");
  expect(!ava::tui::composer_input_cursor_for_screen_position(snapshot, 15, 4) && !ava::tui::detail::transcript_body_screen_geometry(snapshot).valid &&
             !ava::tui::composer_palette_screen_layout(snapshot) && !ava::tui::select_list_selection_for_screen_position(snapshot, 8, 8),
         "command output suppresses composer, transcript, palette, and selector hit targets");

  snapshot.question_prompt = ava::tui::QuestionPromptView{};
  snapshot.question_prompt->header = "Authoritative question";
  snapshot.question_prompt->question = "Choose";
  snapshot.question_prompt->options = {ava::tui::QuestionPromptOptionView{.value = "yes", .label = "Yes"}};
  snapshot.question_prompt->modal = true;
  auto const question_frame = ava::tui::render_composer(snapshot);
  expect(frame_contains(question_frame, "Authoritative question") && !frame_contains(question_frame, "local help output"),
         "permission/question modal authority takes precedence over retained command output");
}

void test_catalog_driven_local_command_transcript_invariant()
{
  auto const baseline = std::vector<ava::tui::TranscriptItem>{ava::tui::TranscriptItem{.label = "you", .text = "seed question"},
                                                              ava::tui::TranscriptItem{.label = "ava", .text = "seed answer"}};
  auto const baseline_semantics = transcript_semantics(baseline);
  bool all_catalog_entries_preserved = true;
  bool all_catalog_outputs_local = true;
  for (auto const& entry : ava::app::command_catalog())
  {
    std::vector<std::string> tokens{entry.command};
    tokens.insert(tokens.end(), entry.aliases.begin(), entry.aliases.end());
    for (auto const& token : tokens)
    {
      auto const policy = ava::tui::tui_submission_projection_policy(true, false);
      ava::tui::ComposerSnapshot snapshot;
      snapshot.transcript = baseline;
      if (policy.present_command_output)
        ava::tui::settle_local_command_completion(snapshot, token + " private argument", {"visible local output"});
      all_catalog_entries_preserved &=
          policy.preserve_transcript && !policy.project_conversation && transcript_semantics(snapshot.transcript) == baseline_semantics;
      all_catalog_outputs_local &= snapshot.command_output.has_value() && snapshot.command_output->title_token == token &&
                                   snapshot.command_output->blocks == std::vector<std::string>{"visible local output"};
    }
  }
  expect(all_catalog_entries_preserved && all_catalog_outputs_local,
         "every built-in slash command and alias uses the centralized local-completion policy without appending transcript");

  for (auto const& submitted : {std::string("!printf secret"), std::string("!!printf secret"), std::string("/compact"), std::string("/new session")})
  {
    ava::tui::ComposerSnapshot snapshot;
    snapshot.transcript = baseline;
    ava::tui::settle_local_command_completion(snapshot, submitted, {"local result"});
    expect(transcript_semantics(snapshot.transcript) == baseline_semantics && snapshot.command_output,
           "shell helpers, compaction, and session command completions preserve transcript and use local output");
  }

  ava::tui::ComposerSnapshot empty_output;
  empty_output.transcript = baseline;
  ava::tui::settle_local_command_completion(empty_output, "/mode", {});
  expect(transcript_semantics(empty_output.transcript) == baseline_semantics && !empty_output.command_output && empty_output.status == "command complete",
         "empty local command output settles as transient status without transcript mutation");

  ava::tui::ComposerSnapshot error_output;
  error_output.transcript = baseline;
  ava::tui::open_command_error(error_output, "/read private-path", "permission denied");
  expect(transcript_semantics(error_output.transcript) == baseline_semantics && error_output.command_output &&
             error_output.command_output->title_token == "/read" && error_output.command_output->blocks.front() == "permission denied",
         "local errors use token-only command output without transcript mutation");

  ava::tui::ComposerSnapshot tool_output;
  tool_output.transcript = baseline;
  auto tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success, .name = "read", .result_summary = "read 4 lines"};
  ava::tui::settle_local_command_completion(tool_output, "/read secret-file", {}, {tool});
  expect(transcript_semantics(tool_output.transcript) == baseline_semantics && tool_output.command_output && tool_output.local_tool_history.size() == 1 &&
             tool_output.command_output->tools.size() == 1,
         "local tool timelines remain in bounded TUI-only command state rather than transcript tool cards");
  for (std::size_t index = 1; index < 55; ++index)
  {
    tool.call_id = "local-tool-" + std::to_string(index);
    ava::tui::settle_local_command_completion(tool_output, "/read private", {}, {tool});
  }
  expect(tool_output.local_tool_history.size() == 50 && tool_output.local_tool_history.front().call_id == "local-tool-5" &&
             tool_output.local_tool_history.back().call_id == "local-tool-54",
         "local command tool history retains only the latest fifty TUI-only tools");

  auto committed_policy = ava::tui::tui_submission_projection_policy(true, true);
  std::vector<ava::tui::TranscriptItem> dynamic_projection = {ava::tui::TranscriptItem{.label = "you", .text = "/dynamic private"},
                                                              ava::tui::TranscriptItem{.label = "you", .text = "expanded provider prompt"},
                                                              ava::tui::TranscriptItem{.label = "ava", .text = "provider answer"}};
  ava::tui::remove_literal_command_invocation(dynamic_projection, "/dynamic private");
  expect(committed_policy.project_conversation && !committed_policy.preserve_transcript && !committed_policy.present_command_output &&
             dynamic_projection.size() == 2 && dynamic_projection.front().text == "expanded provider prompt" &&
             std::ranges::none_of(dynamic_projection, [](ava::tui::TranscriptItem const& item) { return item.text.starts_with('/'); }),
         "committed dynamic prompt commands project genuine conversation but never the literal slash invocation");

  auto normal_policy = ava::tui::tui_submission_projection_policy(false, true);
  expect(normal_policy.project_conversation && !normal_policy.preserve_transcript && !normal_policy.present_command_output,
         "ordinary prompts retain normal conversation projection");
}

void test_buffered_runtime_event_completion_settlement()
{
  auto const baseline = std::vector<ava::tui::TranscriptItem>{ava::tui::TranscriptItem{.label = "you", .text = "prior question"},
                                                              ava::tui::TranscriptItem{.label = "ava", .text = "prior answer"}};
  ava::tui::TuiEventState local_events;
  auto local_user = ava::event::MessagePayload{};
  local_user.text = "/write private.cpp secret";
  ava::tui::apply_runtime_event(local_events, ava::event::RuntimeEvent{{}, ava::event::UserMessageEvent{.payload = std::move(local_user)}});
  auto local_start = ava::event::ToolPayload{};
  local_start.call_id = "local-call";
  local_start.tool = "write";
  local_start.args_json = R"({"path":"private.cpp"})";
  ava::tui::apply_runtime_event(local_events, ava::event::RuntimeEvent{{}, ava::event::ToolStartEvent{.payload = std::move(local_start)}});
  auto local_result = ava::event::ToolPayload{};
  local_result.call_id = "local-call";
  local_result.tool = "write";
  local_result.text = "wrote private.cpp";
  local_result.status = "success";
  local_result.changed_paths = {"private.cpp"};
  ava::tui::apply_runtime_event(local_events, ava::event::RuntimeEvent{{}, ava::event::ToolResultEvent{.payload = std::move(local_result)}});
  auto local_tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success, .name = "write", .call_id = "local-call"};
  auto local = ava::tui::settle_tui_submission(baseline, local_events, "/write private.cpp secret", true, false, {"wrote private.cpp"}, {local_tool}, false);
  expect(local.policy.preserve_transcript && transcript_semantics(local.transcript) == transcript_semantics(baseline) && local.command_tools.size() == 1 &&
             local.command_output == std::vector<std::string>{"wrote private.cpp"} &&
             std::ranges::none_of(local.transcript, [](ava::tui::TranscriptItem const& item) { return item.tool.has_value(); }),
         "production completion settlement withholds local RuntimeEvents and tool timelines from transcript projection");

  ava::tui::TuiEventState dynamic_events;
  auto literal_user = ava::event::MessagePayload{};
  literal_user.text = "/review private argument";
  ava::tui::apply_runtime_event(dynamic_events, ava::event::RuntimeEvent{{}, ava::event::UserMessageEvent{.payload = std::move(literal_user)}});
  auto expanded_user = ava::event::MessagePayload{};
  expanded_user.text = "review the provider-expanded source";
  ava::tui::apply_runtime_event(dynamic_events, ava::event::RuntimeEvent{{}, ava::event::UserMessageEvent{.payload = std::move(expanded_user)}});
  auto provider_tool = ava::event::ToolPayload{};
  provider_tool.call_id = "provider-call";
  provider_tool.tool = "read";
  provider_tool.text = "provider.cpp";
  provider_tool.status = "success";
  ava::tui::apply_runtime_event(dynamic_events, ava::event::RuntimeEvent{{}, ava::event::ToolResultEvent{.payload = std::move(provider_tool)}});
  auto answer = ava::event::MessagePayload{};
  answer.text = "provider answer";
  ava::tui::apply_runtime_event(dynamic_events, ava::event::RuntimeEvent{{}, ava::event::AssistantMessageEvent{.payload = std::move(answer)}});
  auto dynamic = ava::tui::settle_tui_submission(baseline, dynamic_events, "/review private argument", true, true, {}, {}, false);
  expect(dynamic.policy.project_conversation && !dynamic.policy.present_command_output && dynamic.transcript.size() == 3 &&
             dynamic.transcript.front().text == "review the provider-expanded source" && dynamic.transcript[1].tool &&
             dynamic.transcript.back().text == "provider answer" &&
             std::ranges::none_of(dynamic.transcript, [](ava::tui::TranscriptItem const& item) { return item.label == "you" && item.text.starts_with('/'); }),
         "production completion settlement releases committed dynamic provider events and removes only the literal slash invocation");
}

}  // namespace

void run_tui_command_output_tests()
{
  test_command_output_rendering_and_input();
  test_command_output_modal_precedence_and_hits();
  test_catalog_driven_local_command_transcript_invariant();
  test_buffered_runtime_event_completion_settlement();
}
