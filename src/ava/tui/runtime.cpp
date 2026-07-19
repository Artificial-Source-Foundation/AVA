#include "sys.h"
#include "ava/app/EventEnvelope.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/event_state.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/terminal.h"
#include "ava/tui/terminal_image.h"
#include "ava/tui/theme.h"
#include "ava/tui/tool_cards.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <curses.h>
#include <unistd.h>

namespace ava::tui {
namespace {

constexpr std::size_t kMaxBracketedPasteBytes = 1024 * 1024;
constexpr std::size_t kMaxEscapeSequenceBytes = 16 * 1024;
constexpr std::size_t kMaxTranscriptItems = 1000;
constexpr std::size_t kKeyboardScrollRows = 3;
constexpr std::size_t kMouseWheelScrollRows = 1;
constexpr auto kSpinnerFrameDelay = std::chrono::milliseconds(80);
constexpr auto kIdleInputPollDelay = std::chrono::milliseconds(250);
constexpr auto kDisplayReloadPollInterval = std::chrono::milliseconds(500);
constexpr std::string_view kCopyOptionPrefix = "copy:";
constexpr std::string_view kSettingsOpenKeybindings = "settings:keybindings.open";
constexpr std::string_view kSettingsValidateKeybindings = "settings:keybindings.validate";
constexpr std::string_view kSettingsEditKeybindings = "settings:keybindings.edit";
constexpr std::string_view kSettingsReloadKeybindings = "settings:keybindings.reload";
constexpr std::string_view kSettingsOpenModels = "settings:models.open";
constexpr std::string_view kSettingsOpenScopedModels = "settings:models.scoped";
constexpr std::string_view kSettingsTrustStatus = "settings:trust.status";
constexpr std::string_view kSettingsTrustProject = "settings:trust.project";
constexpr std::string_view kSettingsTrustDeny = "settings:trust.deny";
constexpr std::string_view kSettingsTrustClear = "settings:trust.clear";
constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
class SignalBlockGuard
{
 public:
  SignalBlockGuard()
  {
    sigset_t blocked{};
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    active_ = sigprocmask(SIG_BLOCK, &blocked, &previous_) == 0;
  }

  SignalBlockGuard(SignalBlockGuard const&) = delete;
  SignalBlockGuard& operator=(SignalBlockGuard const&) = delete;

  ~SignalBlockGuard()
  {
    if (active_)
      static_cast<void>(sigprocmask(SIG_SETMASK, &previous_, nullptr));
  }

 private:
  sigset_t previous_{};
  bool active_ = false;
};

std::pair<std::size_t, std::size_t> terminal_size()
{
  int height = 0;
  int width = 0;
  getmaxyx(stdscr, height, width);
  if (width > 0 && height > 0)
    return {static_cast<std::size_t>(width), static_cast<std::size_t>(height)};
  return {80, 24};
}

struct CursesInput
{
  InputEvent event;
  std::string text;
  bool bracketed_paste = false;
  bool resize = false;
};

struct PendingPermissionRequest
{
  explicit PendingPermissionRequest(ava::permissions::PermissionPrompt prompt_in) : prompt(std::move(prompt_in)) { }

  ava::permissions::PermissionPrompt prompt;
  std::mutex mutex;
  std::condition_variable ready;
  std::optional<ava::core::Result<ava::permissions::PermissionResolutionDecision>> result;
};

struct PendingQuestionRequest
{
  explicit PendingQuestionRequest(ava::agent::QuestionPrompt prompt_in) : prompt(std::move(prompt_in)) { }

  ava::agent::QuestionPrompt prompt;
  std::mutex mutex;
  std::condition_variable ready;
  std::optional<ava::core::Result<ava::agent::QuestionAnswer>> result;
};

struct EventEnvelopeQueue
{
  std::mutex mutex;
  std::vector<ava::app::EventEnvelope> events;
  bool received = false;

  [[nodiscard]] ava::app::EventEnvelopeSink sink()
  {
    return [this](ava::app::EventEnvelope const& event) -> ava::core::VoidResult {
      std::lock_guard<std::mutex> lock(mutex);
      events.push_back(event);
      received = true;
      return {};
    };
  }

  [[nodiscard]] std::vector<ava::app::EventEnvelope> drain()
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto drained = std::move(events);
    events.clear();
    return drained;
  }

  [[nodiscard]] bool received_any()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return received;
  }
};

enum class RuntimeEventDrainResult
{
  NoEvents,
  UpdatedNoRender,
  Rendered,
  RenderFailed
};
enum class ActiveSelectList
{
  None,
  Hotkeys,
  Settings,
  Model,
  ScopedModels,
  Session
};
enum class ComposerJumpMode
{
  None,
  Forward,
  Backward
};

struct PendingSessionArchiveAction
{
  std::string session_id;
  bool archive = true;
};

bool mouse_state_matches(mmask_t state, mmask_t mask)
{
  return (state & mask) != 0;
}

CursesInput key_input(Key key)
{
  return CursesInput{
      .event = InputEvent{.key = key, .character = '\0', .text = {}, .mouse_column = 0, .mouse_row = 0}, .text = {}, .bracketed_paste = false, .resize = false};
}

CursesInput event_input(InputEvent event)
{
  return CursesInput{.event = std::move(event), .text = {}, .bracketed_paste = false, .resize = false};
}

CursesInput mouse_key_input(Key key, const MEVENT& mouse)
{
  return CursesInput{.event = InputEvent{.key = key,
                                         .character = '\0',
                                         .text = {},
                                         .mouse_column = static_cast<std::size_t>(mouse.x + 1),
                                         .mouse_row = static_cast<std::size_t>(mouse.y + 1)},
                     .text = {},
                     .bracketed_paste = false,
                     .resize = false};
}

CursesInput unknown_input()
{
  return key_input(Key::Unknown);
}

std::optional<std::string> printable_jump_target(CursesInput const& input)
{
  if (input.bracketed_paste)
    return std::nullopt;
  if (input.event.key == Key::Space)
    return std::string(" ");
  if (input.event.key != Key::Character)
    return std::nullopt;

  std::string text = input.text.empty() ? std::string(1, input.event.character) : input.text;
  if (text.empty())
    return std::nullopt;

  auto const first = static_cast<unsigned char>(text.front());
  if (first < 0x20U || first == 0x7FU)
    return std::nullopt;
  if ((first & 0x80U) == 0)
    return text.substr(0, 1);

  auto length = std::size_t{0};
  if (first >= 0xC2U && first <= 0xDFU)
    length = 2;
  else if ((first & 0xF0U) == 0xE0U)
    length = 3;
  else if (first >= 0xF0U && first <= 0xF4U)
    length = 4;
  if (length == 0 || text.size() < length)
    return std::nullopt;
  for (std::size_t index = 1; index < length; ++index)
  {
    if ((static_cast<unsigned char>(text[index]) & 0xC0U) != 0x80U)
      return std::nullopt;
  }
  return text.substr(0, length);
}

bool curses_key_name_equals(int value, std::string_view expected)
{
  char const* name = keyname(value);
  return name != nullptr && std::string_view(name) == expected;
}

std::string title_case_ascii(std::string_view text)
{
  std::string output;
  output.reserve(text.size());
  bool at_word_start = true;
  for (char ch : text)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte == '_' || byte == '-')
    {
      output.push_back(' ');
      at_word_start = true;
      continue;
    }
    if (at_word_start && byte >= 'a' && byte <= 'z')
    {
      output.push_back(static_cast<char>(byte - ('a' - 'A')));
    }
    else
    {
      output.push_back(ch);
    }
    at_word_start = byte == ' ';
  }
  return output;
}

std::string turn_duration_label(std::chrono::steady_clock::duration duration)
{
  auto const milliseconds = std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
  if (milliseconds < 1000)
    return std::to_string(milliseconds) + "ms";

  auto const seconds = milliseconds / 1000;
  if (seconds < 10)
  {
    auto const tenths = (milliseconds % 1000) / 100;
    return std::to_string(seconds) + "." + std::to_string(tenths) + "s";
  }
  if (seconds < 60)
    return std::to_string((milliseconds + 500) / 1000) + "s";

  auto const minutes = seconds / 60;
  auto const remaining_seconds = seconds % 60;
  return std::to_string(minutes) + "m " + std::to_string(remaining_seconds) + "s";
}

std::string assistant_meta_for_snapshot(ComposerSnapshot const& snapshot, std::optional<std::chrono::steady_clock::duration> elapsed = std::nullopt)
{
  if (snapshot.model.empty())
    return {};
  auto mode = title_case_ascii(snapshot.mode);
  if (mode.empty())
    mode = "AVA";
  auto meta = mode + " · " + snapshot.model;
  if (elapsed)
    meta += " · " + turn_duration_label(*elapsed);
  return meta;
}

bool transcript_item_has_content(TranscriptItem const& item)
{
  return !item.text.empty() || !item.thinking.empty() || !text_empty(item.text_model) || !text_empty(item.thinking_model);
}

void apply_assistant_turn_meta(std::vector<TranscriptItem>& transcript, std::string const& meta)
{
  if (meta.empty())
    return;
  for (auto& item : transcript)
  {
    if (!item.tool && item.label == "ava" && transcript_item_has_content(item))
      item.meta = meta;
  }
}

bool is_compact_command(std::string_view line) noexcept
{
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
}

std::size_t transcript_height_for_snapshot(ComposerSnapshot const& snapshot, std::size_t width, std::size_t height)
{
  auto const normal_composer_lines = detail::composer_block_line_count(snapshot, height, width);
  auto const modal_question = snapshot.question_prompt && snapshot.question_prompt->modal;
  auto const prompt_active = snapshot.permission_prompt.has_value() || (snapshot.question_prompt && !modal_question);
  auto const fixed_lines = prompt_active ? std::size_t{0} : normal_composer_lines;
  auto const max_prompt_lines = height > fixed_lines ? height - fixed_lines : 0;
  auto const prompt_line_budget = prompt_active ? std::min<std::size_t>({7, max_prompt_lines}) : 0;
  auto const permission_lines =
      snapshot.permission_prompt ? detail::render_permission_prompt(*snapshot.permission_prompt, width, prompt_line_budget) : std::vector<std::string>{};
  auto const question_lines = snapshot.question_prompt && !modal_question ? detail::render_question_prompt(*snapshot.question_prompt, width, prompt_line_budget)
                                                                          : std::vector<std::string>{};
  auto const fixed_and_prompt_lines = fixed_lines + permission_lines.size() + question_lines.size();
  auto const palette_line_budget = (height > fixed_and_prompt_lines && !prompt_active && !snapshot.slash_palette_suppressed)
                                       ? std::min(detail::kMaxPaletteLines, height - fixed_and_prompt_lines)
                                       : 0;
  auto const palette_lines = detail::render_slash_palette(snapshot, width, palette_line_budget);
  auto const file_reference_palette_lines =
      palette_lines.empty() ? detail::render_file_reference_palette(snapshot, width, palette_line_budget) : std::vector<std::string>{};
  auto const path_completion_palette_lines = palette_lines.empty() && file_reference_palette_lines.empty()
                                                 ? detail::render_path_completion_palette(snapshot, width, palette_line_budget)
                                                 : std::vector<std::string>{};
  auto const active_palette_lines = !palette_lines.empty()                  ? palette_lines.size()
                                    : !file_reference_palette_lines.empty() ? file_reference_palette_lines.size()
                                                                            : path_completion_palette_lines.size();
  auto const non_transcript_lines = fixed_lines + active_palette_lines + permission_lines.size() + question_lines.size();
  return height > non_transcript_lines ? height - non_transcript_lines : 0;
}

std::size_t max_transcript_scroll_offset_for_snapshot(ComposerSnapshot const& snapshot, std::size_t width, std::size_t height)
{
  auto const transcript_height = transcript_height_for_snapshot(snapshot, width, height);
  if (transcript_height == 0)
    return 0;
  auto const rendered_transcript = detail::render_transcript_lines(snapshot.transcript, width, snapshot.tool_details_visible, snapshot.thinking_visible);
  return rendered_transcript.size() > transcript_height ? rendered_transcript.size() - transcript_height : 0;
}

std::optional<std::string> encode_wide_character(wchar_t character)
{
  std::mbstate_t state{};
  char buffer[MB_LEN_MAX]{};
  auto const length = std::wcrtomb(buffer, character, &state);
  if (length == static_cast<std::size_t>(-1))
    return std::nullopt;
  return std::string(buffer, length);
}

std::string base64_encode(std::string_view text)
{
  std::string output;
  output.reserve(((text.size() + 2) / 3) * 4);
  for (std::size_t index = 0; index < text.size(); index += 3)
  {
    auto const first = static_cast<unsigned char>(text[index]);
    auto const second = index + 1 < text.size() ? static_cast<unsigned char>(text[index + 1]) : 0;
    auto const third = index + 2 < text.size() ? static_cast<unsigned char>(text[index + 2]) : 0;
    auto const block = (static_cast<unsigned int>(first) << 16U) | (static_cast<unsigned int>(second) << 8U) | static_cast<unsigned int>(third);
    output.push_back(kBase64Alphabet[(block >> 18U) & 0x3FU]);
    output.push_back(kBase64Alphabet[(block >> 12U) & 0x3FU]);
    output.push_back(index + 1 < text.size() ? kBase64Alphabet[(block >> 6U) & 0x3FU] : '=');
    output.push_back(index + 2 < text.size() ? kBase64Alphabet[block & 0x3FU] : '=');
  }
  return output;
}

bool write_all_to_stdout(std::string_view text)
{
  std::size_t offset = 0;
  while (offset < text.size())
  {
    auto const written = ::write(STDOUT_FILENO, text.data() + offset, text.size() - offset);
    if (written < 0)
    {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (written == 0)
      return false;
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

bool copy_text_to_terminal_clipboard(std::string_view text)
{
  if (text.empty())
    return false;
  auto sequence = std::string("\x1b]52;c;") + base64_encode(text) + "\x1b\\";
  return write_all_to_stdout(sequence);
}

std::optional<std::string_view> copy_text_from_answer(ava::agent::QuestionAnswer const& answer)
{
  for (auto const& option : answer.selected_options)
  {
    if (option.starts_with(kCopyOptionPrefix))
      return std::string_view(option).substr(kCopyOptionPrefix.size());
  }
  return std::nullopt;
}

std::optional<std::string> latest_ava_message_copy_text(std::vector<TranscriptItem> const& transcript)
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (item->label != "ava" && item->label != "assistant")
      continue;
    if (item->text.empty())
      continue;
    return item->text;
  }
  return std::nullopt;
}

std::optional<std::string> latest_tool_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query = {})
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (!item->tool)
      continue;
    if (!detail::tool_card_matches_copy_query(*item->tool, query))
      continue;
    auto text = detail::tool_card_copy_text(*item->tool);
    if (!text.empty())
      return text;
  }
  return std::nullopt;
}

std::optional<ToolTimelineItem> latest_tool_detail_card(std::vector<TranscriptItem> const& transcript, std::string_view query = {})
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (!item->tool)
      continue;
    if (!detail::tool_card_matches_copy_query(*item->tool, query))
      continue;
    auto card = *item->tool;
    card.details_visible = true;
    return card;
  }
  return std::nullopt;
}

std::optional<std::string> latest_tool_diff_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query = {})
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (!item->tool)
      continue;
    if (!detail::tool_card_matches_copy_query(*item->tool, query))
      continue;
    auto text = detail::tool_card_diff_copy_text(*item->tool);
    if (!text.empty())
      return text;
  }
  return std::nullopt;
}

std::string diff_transcript_text(std::string_view title, std::string_view diff)
{
  auto text = std::string(title);
  text += "\n\n```diff\n";
  text += diff;
  if (!diff.empty() && diff.back() != '\n')
    text += '\n';
  text += "```";
  return text;
}

std::optional<std::string> latest_permission_copy_text(std::vector<TranscriptItem> const& transcript, std::string_view query = {})
{
  for (auto item = transcript.rbegin(); item != transcript.rend(); ++item)
  {
    if (!item->tool)
      continue;
    auto text = detail::tool_card_permission_copy_text(*item->tool, query);
    if (!text.empty())
      return text;
  }
  return std::nullopt;
}

std::string question_answer_audit_detail(ava::agent::QuestionAnswer const& answer)
{
  auto detail = std::string("question answered");
  if (!answer.selected_options.empty())
  {
    auto const first = std::string_view(answer.selected_options.front());
    detail += first.starts_with(kCopyOptionPrefix) ? ": copy" : ": " + answer.selected_options.front();
  }
  if (!answer.custom_text.empty())
    detail += answer.selected_options.empty() ? ": custom" : ", custom";
  return detail;
}

std::string_view first_ascii_token(std::string_view text)
{
  auto const end = text.find_first_of(" \t\r\n");
  return text.substr(0, end == std::string_view::npos ? text.size() : end);
}

bool should_echo_slash_command(std::string_view submitted)
{
  auto const token = first_ascii_token(submitted);
  return token != "/connect" && token != "/login";
}

bool shell_helper_submission(std::string_view submitted)
{
  return submitted.starts_with('!');
}

bool should_show_slash_command_output_as_status(std::string_view submitted)
{
  auto const token = first_ascii_token(submitted);
  return token == "/connect" || token == "/login";
}

std::vector<std::string> split_key_display(std::string_view keys)
{
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= keys.size())
  {
    auto const comma = keys.find(',', start);
    auto end = comma == std::string_view::npos ? keys.size() : comma;
    while (start < end && std::isspace(static_cast<unsigned char>(keys[start])) != 0) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(keys[end - 1])) != 0) --end;
    if (start < end)
      parts.emplace_back(keys.substr(start, end - start));
    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  return parts;
}

std::size_t shared_key_count(std::vector<TuiKeyBindingHelpItem> const& items, std::string_view key)
{
  return static_cast<std::size_t>(std::ranges::count_if(items, [&](TuiKeyBindingHelpItem const& item) {
    auto const keys = split_key_display(item.keys);
    return std::ranges::find(keys, key) != keys.end();
  }));
}

std::string value_or_unknown(std::string value)
{
  return value.empty() ? std::string("unknown") : std::move(value);
}

std::string compact_path_leaf(std::string path)
{
  while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
  auto const slash = path.find_last_of("/\\");
  if (slash == std::string::npos)
    return path;
  if (slash + 1 >= path.size())
    return path;
  return path.substr(slash + 1);
}

bool exact_command(std::string_view submitted, std::string_view command)
{
  while (!submitted.empty() && std::isspace(static_cast<unsigned char>(submitted.front())) != 0) submitted.remove_prefix(1);
  while (!submitted.empty() && std::isspace(static_cast<unsigned char>(submitted.back())) != 0) submitted.remove_suffix(1);
  return submitted == command;
}

bool starts_with_command_submission(std::string_view submitted, std::string_view command)
{
  while (!submitted.empty() && std::isspace(static_cast<unsigned char>(submitted.front())) != 0) submitted.remove_prefix(1);
  while (!submitted.empty() && std::isspace(static_cast<unsigned char>(submitted.back())) != 0) submitted.remove_suffix(1);
  return submitted == command ||
         (submitted.starts_with(command) && submitted.size() > command.size() && std::isspace(static_cast<unsigned char>(submitted[command.size()])) != 0);
}

bool session_switching_command(std::string_view submitted)
{
  return starts_with_command_submission(submitted, "/new") || starts_with_command_submission(submitted, "/resume") ||
         starts_with_command_submission(submitted, "/fork") || starts_with_command_submission(submitted, "/clone");
}

std::string trim_view_to_string(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
  return std::string(text);
}

std::optional<std::string> reload_command_argument(std::string_view submitted)
{
  auto const normalized = trim_view_to_string(submitted);
  submitted = normalized;
  constexpr std::string_view kReload = "/reload";
  if (submitted == kReload)
    return std::string{};
  if (submitted.starts_with(kReload) && submitted.size() > kReload.size() && std::isspace(static_cast<unsigned char>(submitted[kReload.size()])) != 0)
  {
    return trim_view_to_string(submitted.substr(kReload.size() + 1));
  }
  return std::nullopt;
}

std::optional<std::string> copy_command_argument(std::string_view submitted)
{
  auto const normalized = trim_view_to_string(submitted);
  submitted = normalized;
  constexpr std::string_view kCopy = "/copy";
  if (submitted == kCopy)
    return std::string{};
  if (submitted.starts_with(kCopy) && submitted.size() > kCopy.size() && std::isspace(static_cast<unsigned char>(submitted[kCopy.size()])) != 0)
  {
    return trim_view_to_string(submitted.substr(kCopy.size() + 1));
  }
  return std::nullopt;
}

std::optional<std::string> tool_command_argument(std::string_view submitted)
{
  auto const normalized = trim_view_to_string(submitted);
  submitted = normalized;
  for (auto const command : {std::string_view("/tool"), std::string_view("/tools")})
  {
    if (submitted == command)
      return std::string{};
    if (submitted.starts_with(command) && submitted.size() > command.size() && std::isspace(static_cast<unsigned char>(submitted[command.size()])) != 0)
    {
      return trim_view_to_string(submitted.substr(command.size() + 1));
    }
  }
  return std::nullopt;
}

std::optional<std::string> diff_command_argument(std::string_view submitted)
{
  auto const normalized = trim_view_to_string(submitted);
  submitted = normalized;
  constexpr std::string_view kDiff = "/diff";
  if (submitted == kDiff)
    return std::string{};
  if (submitted.starts_with(kDiff) && submitted.size() > kDiff.size() && std::isspace(static_cast<unsigned char>(submitted[kDiff.size()])) != 0)
  {
    return trim_view_to_string(submitted.substr(kDiff.size() + 1));
  }
  return std::nullopt;
}

std::optional<std::string> attach_command_argument(std::string_view submitted)
{
  auto const normalized = trim_view_to_string(submitted);
  submitted = normalized;
  for (auto const command : {std::string_view("/attach"), std::string_view("/image")})
  {
    if (submitted == command)
      return std::string{};
    if (submitted.starts_with(command) && submitted.size() > command.size() && std::isspace(static_cast<unsigned char>(submitted[command.size()])) != 0)
    {
      auto argument = trim_view_to_string(submitted.substr(command.size() + 1));
      if (argument.size() >= 2 && ((argument.front() == '"' && argument.back() == '"') || (argument.front() == '\'' && argument.back() == '\'')))
      {
        argument = argument.substr(1, argument.size() - 2);
      }
      return argument;
    }
  }
  return std::nullopt;
}

std::string attachment_detail(ava::session::ImageAttachmentRef const& attachment, TerminalImageCapabilities const& capabilities)
{
  return "(" + attachment.mime_type + ", " + std::to_string(attachment.byte_size) + " bytes) preview " + terminal_image_settings_description(capabilities);
}

std::optional<std::size_t> kitty_image_id_from_sha(std::string_view sha)
{
  std::size_t value = 0;
  std::size_t digits = 0;
  for (char const ch : sha)
  {
    auto const byte = static_cast<unsigned char>(ch);
    int nibble = -1;
    if (byte >= '0' && byte <= '9')
    {
      nibble = byte - '0';
    }
    else if (byte >= 'a' && byte <= 'f')
    {
      nibble = 10 + byte - 'a';
    }
    else if (byte >= 'A' && byte <= 'F')
    {
      nibble = 10 + byte - 'A';
    }
    else
    {
      break;
    }
    value = (value << 4U) | static_cast<std::size_t>(nibble);
    ++digits;
    if (digits == 8)
      break;
  }
  if (digits == 0)
    return std::nullopt;
  return value == 0 ? std::size_t{1} : value;
}

std::optional<PendingAttachmentItem::Preview> attachment_preview(
    ava::session::ImageAttachmentRef const& attachment, TerminalImageCapabilities const& capabilities,
    std::function<ava::core::Result<ava::session::LoadedImageAttachment>(ava::session::ImageAttachmentRef const&)> const& load_image_attachment)
{
  if (capabilities.images == TerminalImageProtocol::None || !load_image_attachment)
    return std::nullopt;
  auto loaded = load_image_attachment(attachment);
  if (!loaded)
    return std::nullopt;
  auto dimensions = image_dimensions_from_bytes(loaded->bytes, attachment.mime_type).value_or(ImageDimensions{.width_px = 800, .height_px = 600});
  return PendingAttachmentItem::Preview{.protocol = capabilities.images,
                                        .base64_data = std::make_shared<std::string const>(base64_encode(loaded->bytes)),
                                        .dimensions = dimensions,
                                        .image_id = kitty_image_id_from_sha(attachment.sha256)};
}

struct CopyTarget
{
  std::string name;
  std::string query = {};
};

CopyTarget parse_copy_target(std::string_view argument)
{
  auto const normalized = trim_view_to_string(argument);
  auto view = std::string_view(normalized);
  auto const name_end = view.find_first_of(" \t\r\n");
  if (name_end == std::string_view::npos)
    return CopyTarget{.name = normalized};
  return CopyTarget{.name = std::string(view.substr(0, name_end)), .query = trim_view_to_string(view.substr(name_end + 1))};
}

enum class ReloadTarget
{
  KeyBindings,
  DisplaySettings,
};

std::optional<ReloadTarget> reload_target_from_argument(std::string_view target)
{
  auto const normalized = trim_view_to_string(target);
  if (normalized.empty() || normalized == "keybindings" || normalized == "keybinds" || normalized == "keys")
    return ReloadTarget::KeyBindings;
  if (normalized == "theme" || normalized == "themes" || normalized == "display")
    return ReloadTarget::DisplaySettings;
  return std::nullopt;
}

void add_settings_row(SelectListView& view, std::string group, std::string label, std::string description, std::string detail = {}, std::string badge = {})
{
  view.items.push_back(SelectListItemView{.value = group + ":" + label,
                                          .label = std::move(label),
                                          .description = std::move(description),
                                          .group = std::move(group),
                                          .detail = std::move(detail),
                                          .badge = std::move(badge),
                                          .current = false,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

void add_settings_action_row(SelectListView& view, std::string value, std::string group, std::string label, std::string description, std::string detail = {},
                             std::string badge = {})
{
  view.items.push_back(SelectListItemView{.value = std::move(value),
                                          .label = std::move(label),
                                          .description = std::move(description),
                                          .group = std::move(group),
                                          .detail = std::move(detail),
                                          .badge = std::move(badge),
                                          .current = false,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

void add_theme_settings_action(SelectListView& view, std::string value, std::string label, std::string description, bool current)
{
  view.items.push_back(SelectListItemView{.value = "theme:" + value,
                                          .label = std::move(label),
                                          .description = std::move(description),
                                          .group = "Display",
                                          .detail = "persist to display.json",
                                          .badge = current ? std::string("current") : std::string("select"),
                                          .current = current,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

void add_custom_theme_settings_action(SelectListView& view, ThemeOptionItem const& theme, bool current)
{
  view.items.push_back(SelectListItemView{.value = "theme:" + theme.name,
                                          .label = "Theme " + theme.name,
                                          .description = "custom theme",
                                          .group = "Display",
                                          .detail = theme.detail.empty() ? std::string("persist to display.json") : theme.detail,
                                          .badge = current ? std::string("current") : std::string("select"),
                                          .current = current,
                                          .enabled = true,
                                          .disabled_reason = {}});
}

CursesInput character_input(std::string text, bool bracketed_paste = false)
{
  auto const first_byte = text.empty() ? '\0' : text[0];
  auto event_text = text;
  return CursesInput{.event = InputEvent{.key = Key::Character, .character = first_byte, .text = std::move(event_text), .mouse_column = 0, .mouse_row = 0},
                     .text = std::move(text),
                     .bracketed_paste = bracketed_paste,
                     .resize = false};
}

CursesInput space_input()
{
  return CursesInput{.event = InputEvent{.key = Key::Space, .character = ' ', .text = " ", .mouse_column = 0, .mouse_row = 0},
                     .text = " ",
                     .bracketed_paste = false,
                     .resize = false};
}

std::optional<wchar_t> read_plain_wide_character()
{
  wint_t value = 0;
  auto const result = wget_wch(stdscr, &value);
  if (result == ERR || result == KEY_CODE_YES)
    return std::nullopt;
  return static_cast<wchar_t>(value);
}

std::pair<bool, std::string> read_ascii_sequence(std::string_view expected)
{
  std::string consumed;
  consumed.reserve(expected.size());
  for (auto const expected_char : expected)
  {
    auto const character = read_plain_wide_character();
    if (!character)
      return {false, consumed};
    auto encoded = encode_wide_character(*character);
    if (encoded)
      consumed += *encoded;
    if (*character != static_cast<unsigned char>(expected_char))
      return {false, consumed};
  }
  return {true, consumed};
}

CursesInput read_bracketed_paste()
{
  std::string pasted;
  static_cast<void>(wtimeout(stdscr, 1000));
  while (!terminal_signal_received() && pasted.size() < kMaxBracketedPasteBytes)
  {
    auto const character = read_plain_wide_character();
    if (!character)
      break;
    if (*character == L'\x1b')
    {
      auto [matched_end, consumed] = read_ascii_sequence("[201~");
      if (matched_end)
        break;
      pasted.push_back('\x1b');
      if (pasted.size() + consumed.size() > kMaxBracketedPasteBytes)
      {
        break;
      }
      pasted += consumed;
      continue;
    }
    if (auto encoded = encode_wide_character(*character))
    {
      if (pasted.size() + encoded->size() > kMaxBracketedPasteBytes)
      {
        break;
      }
      pasted += *encoded;
    }
  }
  static_cast<void>(wtimeout(stdscr, -1));
  return character_input(normalize_composer_paste_text(pasted), true);
}

std::optional<CursesInput> read_escape_sequence_input()
{
  static_cast<void>(wtimeout(stdscr, 50));
  std::string consumed;
  consumed.reserve(32);
  while (consumed.size() < kMaxEscapeSequenceBytes)
  {
    wint_t value = 0;
    auto const result = wget_wch(stdscr, &value);
    if (result == ERR)
      break;
    if (result == KEY_CODE_YES)
    {
      if (static_cast<int>(value) == KEY_BACKSPACE)
      {
        consumed.push_back('\x7f');
      }
      else
      {
        break;
      }
    }
    else if (auto encoded = encode_wide_character(static_cast<wchar_t>(value)))
    {
      consumed += *encoded;
    }
    if (terminal_escape_sequence_complete(consumed))
      break;
  }
  static_cast<void>(wtimeout(stdscr, -1));

  if (consumed.empty())
    return std::nullopt;
  if (terminal_keyboard_protocol_handle_response(consumed))
    return unknown_input();
  if (consumed == "[200~")
    return read_bracketed_paste();
  if (auto event = terminal_escape_sequence_event(consumed); event.key != Key::Unknown)
    return event_input(std::move(event));
  if (terminal_escape_sequence_should_discard(consumed) || !terminal_escape_sequence_complete(consumed))
  {
    return unknown_input();
  }
  return unknown_input();
}

CursesInput read_curses_input()
{
  wint_t value = 0;
  auto const result = wget_wch(stdscr, &value);
  if (terminal_signal_received())
    return key_input(Key::CtrlC);
  if (result == ERR)
    return unknown_input();

  if (result == KEY_CODE_YES)
  {
    if (curses_key_name_equals(static_cast<int>(value), "kDC2"))
    {
      return key_input(Key::ShiftDelete);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kDC3"))
    {
      return key_input(Key::AltDelete);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kLFT6"))
    {
      return key_input(Key::ShiftCtrlArrowLeft);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kRIT6"))
    {
      return key_input(Key::ShiftCtrlArrowRight);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kLFT4"))
    {
      return key_input(Key::ShiftAltArrowLeft);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kRIT4"))
    {
      return key_input(Key::ShiftAltArrowRight);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kLFT3"))
    {
      return key_input(Key::AltArrowLeft);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kRIT3"))
    {
      return key_input(Key::AltArrowRight);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kUP3"))
    {
      return key_input(Key::AltArrowUp);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kDN3"))
    {
      return key_input(Key::AltArrowDown);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kUP2"))
    {
      return key_input(Key::ShiftArrowUp);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kDN2"))
    {
      return key_input(Key::ShiftArrowDown);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kri"))
    {
      return key_input(Key::ShiftArrowUp);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kind"))
    {
      return key_input(Key::ShiftArrowDown);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kLFT2"))
    {
      return key_input(Key::ShiftArrowLeft);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kRIT2"))
    {
      return key_input(Key::ShiftArrowRight);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kLFT5"))
    {
      return key_input(Key::CtrlArrowLeft);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kRIT5"))
    {
      return key_input(Key::CtrlArrowRight);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kHOM6"))
    {
      return key_input(Key::ShiftCtrlHome);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kEND6"))
    {
      return key_input(Key::ShiftCtrlEnd);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kHOM5"))
    {
      return key_input(Key::CtrlHome);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kEND5"))
    {
      return key_input(Key::CtrlEnd);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kHOM2"))
    {
      return key_input(Key::ShiftHome);
    }
    if (curses_key_name_equals(static_cast<int>(value), "kEND2"))
    {
      return key_input(Key::ShiftEnd);
    }
    switch (static_cast<int>(value))
    {
      case KEY_ENTER:
        return key_input(Key::Enter);
      case KEY_BACKSPACE:
        return key_input(Key::Backspace);
#ifdef KEY_BTAB
      case KEY_BTAB:
        return key_input(Key::ShiftTab);
#endif
#if defined(KEY_SDC) && (!defined(KEY_DC) || KEY_SDC != KEY_DC)
      case KEY_SDC:
        return key_input(Key::ShiftDelete);
#endif
#ifdef KEY_DC
      case KEY_DC:
        return key_input(Key::Delete);
#endif
#ifdef KEY_IC
      case KEY_IC:
        return key_input(Key::Insert);
#endif
#ifdef KEY_CLEAR
      case KEY_CLEAR:
        return key_input(Key::Clear);
#endif
      case KEY_UP:
        return key_input(Key::ArrowUp);
      case KEY_DOWN:
        return key_input(Key::ArrowDown);
      case KEY_LEFT:
        return key_input(Key::ArrowLeft);
      case KEY_RIGHT:
        return key_input(Key::ArrowRight);
#ifdef KEY_SLEFT
      case KEY_SLEFT:
        return key_input(Key::ShiftArrowLeft);
#endif
#ifdef KEY_SRIGHT
      case KEY_SRIGHT:
        return key_input(Key::ShiftArrowRight);
#endif
#ifdef KEY_SUP
      case KEY_SUP:
        return key_input(Key::ShiftArrowUp);
#endif
#ifdef KEY_SDOWN
      case KEY_SDOWN:
        return key_input(Key::ShiftArrowDown);
#endif
#ifdef KEY_SR
      case KEY_SR:
        return key_input(Key::ShiftArrowUp);
#endif
#ifdef KEY_SF
      case KEY_SF:
        return key_input(Key::ShiftArrowDown);
#endif
#ifdef KEY_SHOME
      case KEY_SHOME:
        return key_input(Key::ShiftHome);
#endif
#ifdef KEY_SEND
      case KEY_SEND:
        return key_input(Key::ShiftEnd);
#endif
      case KEY_PPAGE:
        return key_input(Key::PageUp);
      case KEY_NPAGE:
        return key_input(Key::PageDown);
#ifdef KEY_HOME
      case KEY_HOME:
        return key_input(Key::Home);
#endif
#ifdef KEY_END
      case KEY_END:
        return key_input(Key::End);
#endif
#ifdef KEY_F
      case KEY_F(1):
        return key_input(Key::F1);
      case KEY_F(2):
        return key_input(Key::F2);
      case KEY_F(3):
        return key_input(Key::F3);
      case KEY_F(4):
        return key_input(Key::F4);
      case KEY_F(5):
        return key_input(Key::F5);
      case KEY_F(6):
        return key_input(Key::F6);
      case KEY_F(7):
        return key_input(Key::F7);
      case KEY_F(8):
        return key_input(Key::F8);
      case KEY_F(9):
        return key_input(Key::F9);
      case KEY_F(10):
        return key_input(Key::F10);
      case KEY_F(11):
        return key_input(Key::F11);
      case KEY_F(12):
        return key_input(Key::F12);
#endif
#ifdef KEY_RESIZE
      case KEY_RESIZE:
        return CursesInput{.event = InputEvent{.key = Key::Unknown, .character = '\0', .text = {}, .mouse_column = 0, .mouse_row = 0},
                           .text = {},
                           .bracketed_paste = false,
                           .resize = true};
#endif
#ifdef KEY_MOUSE
      case KEY_MOUSE: {
        MEVENT mouse{};
        if (getmouse(&mouse) != OK)
          return unknown_input();
        if (mouse_state_matches(mouse.bstate, BUTTON4_PRESSED))
        {
          return mouse_key_input(Key::MouseWheelUp, mouse);
        }
        if (mouse_state_matches(mouse.bstate, BUTTON5_PRESSED))
        {
          return mouse_key_input(Key::MouseWheelDown, mouse);
        }
        if ((mouse.bstate & BUTTON1_CLICKED) != 0)
        {
          return mouse_key_input(Key::MouseLeftClick, mouse);
        }
        return unknown_input();
      }
#endif
      default:
        return unknown_input();
    }
  }

  auto const character = static_cast<wchar_t>(value);
  if (character == L'\r')
    return key_input(Key::Enter);
  if (character == L'\n')
    return key_input(Key::ShiftEnter);
  if (character == L'\t')
    return key_input(Key::Tab);
  if (character == L' ')
    return space_input();
  if (character == 0x00)
    return key_input(Key::CtrlSpace);
  if (character == 0x1B)
  {
    if (auto escape_input = read_escape_sequence_input())
      return *escape_input;
    return key_input(Key::Escape);
  }
  if (character == 0x01)
    return key_input(Key::CtrlA);
  if (character == 0x02)
    return key_input(Key::CtrlB);
  if (character == 0x03)
    return key_input(Key::CtrlC);
  if (character == 0x04)
    return key_input(Key::CtrlD);
  if (character == 0x05)
    return key_input(Key::CtrlE);
  if (character == 0x06)
    return key_input(Key::CtrlF);
  if (character == 0x07)
    return key_input(Key::CtrlG);
  if (character == 0x08)
    return key_input(Key::CtrlH);
  if (character == 0x0B)
    return key_input(Key::CtrlK);
  if (character == 0x0C)
    return key_input(Key::CtrlL);
  if (character == 0x1F)
    return key_input(Key::CtrlMinus);
  if (character == 0x0E)
    return key_input(Key::CtrlN);
  if (character == 0x0F)
    return key_input(Key::CtrlO);
  if (character == 0x10)
    return key_input(Key::CtrlP);
  if (character == 0x12)
    return key_input(Key::CtrlR);
  if (character == 0x13)
    return key_input(Key::CtrlS);
  if (character == 0x14)
    return key_input(Key::CtrlT);
  if (character == 0x15)
    return key_input(Key::CtrlU);
  if (character == 0x16)
    return key_input(Key::CtrlV);
  if (character == 0x17)
    return key_input(Key::CtrlW);
  if (character == 0x18)
    return key_input(Key::CtrlX);
  if (character == 0x19)
    return key_input(Key::CtrlY);
  if (character == 0x1A)
    return key_input(Key::CtrlZ);
  if (character == 0x1D)
    return key_input(Key::CtrlRightBracket);
  if (character == 0x7F)
    return key_input(Key::Backspace);
  if (character >= 0x20)
  {
    auto encoded = encode_wide_character(character);
    if (!encoded)
      return unknown_input();
    return character_input(std::move(*encoded));
  }
  return unknown_input();
}

bool empty_curses_input(CursesInput const& input)
{
  return !input.resize && input.event.key == Key::Unknown && input.text.empty() && !input.bracketed_paste;
}

std::optional<CursesInput> poll_curses_input()
{
  static_cast<void>(wtimeout(stdscr, 0));
  auto input = read_curses_input();
  static_cast<void>(wtimeout(stdscr, -1));
  if (empty_curses_input(input))
  {
    return std::nullopt;
  }
  return input;
}

std::optional<CursesInput> read_curses_input_with_timeout(std::chrono::milliseconds timeout)
{
  static_cast<void>(wtimeout(stdscr, static_cast<int>(timeout.count())));
  auto input = read_curses_input();
  static_cast<void>(wtimeout(stdscr, -1));
  if (empty_curses_input(input))
    return std::nullopt;
  return input;
}

void truncate_transcript(std::vector<TranscriptItem>& transcript)
{
  if (transcript.size() > kMaxTranscriptItems)
  {
    transcript.erase(transcript.begin(), transcript.begin() + static_cast<std::ptrdiff_t>(transcript.size() - kMaxTranscriptItems));
  }
}

void push_transcript(ComposerSnapshot& snapshot, TranscriptItem item)
{
  snapshot.transcript.push_back(std::move(item));
  truncate_transcript(snapshot.transcript);
}

void push_history(std::vector<std::string>& history, std::string input)
{
  static_cast<void>(push_composer_input_history(history, std::move(input)));
}

PermissionPromptView permission_prompt_view(ava::permissions::PermissionPrompt const& prompt)
{
  PermissionPromptView view;
  view.tool_name = prompt.tool_name;
  view.operation = ava::permissions::to_string(prompt.operation);
  view.target = prompt.target_path.empty() ? std::string{} : prompt.target_path.generic_string();
  view.command = prompt.command;
  view.reason = prompt.reason;
  view.risk = ava::permissions::to_string(prompt.risk);
  view.request_id = prompt.permission_request_id;
  view.diff_preview = prompt.diff_preview;
  view.diff_truncated = prompt.diff_truncated;
  if (prompt.command_metadata)
  {
    view.recipe_display = prompt.command_metadata->recipe_display;
    view.workspace_recipe_key = prompt.command_metadata->workspace_recipe_key;
    for (std::size_t index = 0; index < prompt.command_metadata->effective_allowed_scopes.size(); ++index)
    {
      if (index > 0)
        view.effective_allowed_scopes += ", ";
      view.effective_allowed_scopes += ava::command::to_string(prompt.command_metadata->effective_allowed_scopes[index]);
    }
  }
  return view;
}

QuestionPromptView question_prompt_view(ava::agent::QuestionPrompt const& prompt)
{
  QuestionPromptView view;
  view.header = prompt.header;
  view.question = prompt.question;
  view.multiple = prompt.multiple;
  view.allow_custom = prompt.allow_custom;
  view.secret = prompt.secret;
  view.modal = prompt.modal;
  view.searchable = prompt.searchable;
  view.options.reserve(prompt.options.size());
  for (auto const& option : prompt.options)
  {
    view.options.push_back(QuestionPromptOptionView{.value = option.value, .label = option.label, .selected = false});
  }
  return view;
}

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_view(QuestionPromptView const& prompt)
{
  ava::agent::QuestionAnswer answer;
  for (auto const& option : prompt.options)
  {
    if (option.selected)
      answer.selected_options.push_back(option.value);
  }

  if (prompt.allow_custom && !prompt.custom_text.empty() && (!prompt.searchable || answer.selected_options.empty()))
  {
    answer.custom_text = prompt.custom_text;
  }

  if (!prompt.multiple && answer.selected_options.empty() && answer.custom_text.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt resolved without an answer"));
  }

  return answer;
}

}  // namespace

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(QuestionPromptView const& prompt)
{
  return question_answer_from_view(prompt);
}

SelectListView hotkeys_select_list_view(TuiKeyBindings const& bindings, std::string footer_hint)
{
  auto const help_items = key_binding_help_items(bindings);
  SelectListView view{
      .title = "Keybindings",
      .subtitle =
          "Active TUI bindings · Enter drafts /keybindings set · config $XDG_CONFIG_HOME/ava/keybinds.json · init /keybindings init · reload /reload "
          "keybindings",
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search keybindings",
      .empty_text = "No keybindings match",
      .footer_hint = footer_hint.empty() ? std::string("Type to filter · PgUp/PgDn page · Enter draft edit · Esc close") : std::move(footer_hint)};
  view.items.reserve(help_items.size());
  for (auto const& item : help_items)
  {
    auto badge = std::string("active");
    for (auto const& key : split_key_display(item.keys))
    {
      if (shared_key_count(help_items, key) > 1)
      {
        badge = "shared key";
        break;
      }
    }
    view.items.push_back(SelectListItemView{.value = item.action,
                                            .label = item.action,
                                            .description = item.description,
                                            .group = "Hotkeys",
                                            .detail = item.keys,
                                            .badge = std::move(badge),
                                            .current = false,
                                            .enabled = true,
                                            .disabled_reason = {}});
  }
  if (view.items.empty())
  {
    view.items.push_back(SelectListItemView{.value = {},
                                            .label = "No active hotkeys",
                                            .description = "Keybinding loader returned no displayable bindings",
                                            .group = "Hotkeys",
                                            .detail = {},
                                            .badge = {},
                                            .current = false,
                                            .enabled = false,
                                            .disabled_reason = "no bindings"});
  }
  return view;
}

SelectListView settings_select_list_view(ComposerSnapshot const& snapshot, std::string footer_hint)
{
  return settings_select_list_view(snapshot, default_key_bindings(), std::move(footer_hint));
}

SelectListView settings_select_list_view(ComposerSnapshot const& snapshot, TuiKeyBindings const& bindings, std::string footer_hint)
{
  SelectListView view{
      .title = "Settings",
      .subtitle = "Runtime view · Enter applies action rows · backend commands own config/session/provider changes",
      .items = {},
      .selected_item_index = 0,
      .query = {},
      .placeholder = "Search settings",
      .empty_text = "No settings match",
      .footer_hint = footer_hint.empty() ? std::string("Type to filter · PgUp/PgDn page · Enter apply/open/draft · Esc close") : std::move(footer_hint)};
  view.items.reserve(25);

  auto const sidebar = snapshot.sidebar;
  auto const theme = active_tui_theme();
  add_settings_row(view, "Display", "Theme", theme.name, theme.detail, theme.badge);
  add_theme_settings_action(view, "dark", "Theme dark", "built-in dark ncurses token palette", theme.kind == TuiThemeKind::Dark);
  add_theme_settings_action(view, "light", "Theme light", "built-in light ncurses token palette", theme.kind == TuiThemeKind::Light);
  add_theme_settings_action(view, "plain", "Theme plain", "disable ANSI styling in rendered frames", theme.kind == TuiThemeKind::Plain);
  add_theme_settings_action(view, "reset", "Theme reset", "clear display.json theme and use built-in default", false);
  for (auto const& custom_theme : snapshot.custom_themes)
  {
    add_custom_theme_settings_action(view, custom_theme, theme.kind == TuiThemeKind::Custom && custom_theme.name == theme.name);
  }
  auto const image_capabilities = active_terminal_image_capabilities();
  add_settings_row(view, "Display", "Image preview", terminal_image_settings_description(image_capabilities),
                   terminal_image_settings_detail(image_capabilities), image_capabilities.badge);
  add_settings_row(view, "Display", "Tool details", snapshot.tool_details_visible ? "expanded" : "compact", "toggle with Ctrl+O or /details");
  add_settings_row(view, "Display", "Thinking blocks", snapshot.thinking_visible ? "visible" : "hidden", "toggle with /thinking");

  auto const binding_count = key_binding_help_items(bindings).size();
  add_settings_action_row(view, std::string(kSettingsOpenKeybindings), "Input", "Keybindings",
                          std::to_string(binding_count) + (binding_count == 1 ? " active action" : " active actions"), "Enter opens active bindings", "open");
  add_settings_action_row(view, std::string(kSettingsValidateKeybindings), "Input", "Keybindings file", "$XDG_CONFIG_HOME/ava/keybinds.json",
                          "Enter validates with /keybindings validate", "validate");
  add_settings_action_row(view, std::string(kSettingsEditKeybindings), "Input", "Keybindings edit", "/keybindings set <action> <key>",
                          "Enter drafts the edit command; reset removes one override", "draft");
  add_settings_action_row(view, std::string(kSettingsReloadKeybindings), "Input", "Keybindings reload", "/reload keybindings",
                          "Enter applies valid keybinds.json edits", "live");

  add_settings_row(view, "Runtime", "Mode", value_or_unknown(snapshot.mode), "use /mode or Tab between turns");
  add_settings_row(view, "Runtime", "Model", value_or_unknown(snapshot.provider) + "/" + value_or_unknown(snapshot.model),
                   snapshot.reasoning_status.value_or("reasoning default"));
  add_settings_action_row(view, std::string(kSettingsOpenModels), "Runtime", "Model selector",
                          value_or_unknown(snapshot.provider) + "/" + value_or_unknown(snapshot.model), "Enter opens /models selector", "open");
  add_settings_action_row(view, std::string(kSettingsOpenScopedModels), "Runtime", "Model cycle scope", "Ctrl+P scoped cycle",
                          "Enter opens /scoped-models; Ctrl+S saves models.json", "open");
  add_settings_row(view, "Runtime", "Session", value_or_unknown(snapshot.session_id),
                   sidebar && !sidebar->session_path.empty() ? sidebar->session_path : std::string("path unavailable"));
  if (sidebar && sidebar->session_entry_count)
  {
    add_settings_row(view, "Runtime", "Session entries", std::to_string(*sidebar->session_entry_count));
  }
  add_settings_row(view, "Runtime", "Token status", snapshot.token_status.value_or("tokens unknown"));
  add_settings_row(view, "Runtime", "Context sources",
                   sidebar && sidebar->context_source_count ? std::to_string(*sidebar->context_source_count) : std::string("unknown"));

  add_settings_row(view, "Workspace", "Current directory", sidebar && !sidebar->workspace.empty() ? sidebar->workspace : std::string("unknown"),
                   sidebar && !sidebar->workspace.empty() ? compact_path_leaf(sidebar->workspace) : std::string{});
  add_settings_row(view, "Workspace", "Git branch", sidebar && !sidebar->git_branch.empty() ? sidebar->git_branch : std::string("not detected"));
  if (snapshot.project_trust)
  {
    auto const& trust = *snapshot.project_trust;
    auto const resources = trust.protected_resource_count == 1 ? std::string("1 protected project resource")
                                                               : std::to_string(trust.protected_resource_count) + " protected project resources";
    add_settings_row(view, "Workspace", "Project trust", value_or_unknown(trust.decision), "project resources " + value_or_unknown(trust.project_resources),
                     value_or_unknown(trust.decision));
    add_settings_row(view, "Workspace", "Protected resources", resources,
                     trust.matched_path.empty() ? std::string("no saved decision matched this workspace") : "matched " + trust.matched_path);
    if (!trust.diagnostic.empty())
      add_settings_row(view, "Workspace", "Trust diagnostic", trust.diagnostic);
    add_settings_action_row(view, std::string(kSettingsTrustStatus), "Workspace", "Trust status", "/trust status", "Enter prints project trust diagnostics",
                            "status");
    add_settings_action_row(view, std::string(kSettingsTrustProject), "Workspace", "Trust project", "allow this workspace's project resources",
                            "Enter runs /trust project", "trust");
    add_settings_action_row(view, std::string(kSettingsTrustDeny), "Workspace", "Deny project", "keep this workspace's project resources skipped",
                            "Enter runs /trust deny", "deny");
    add_settings_action_row(view, std::string(kSettingsTrustClear), "Workspace", "Clear trust decision", "remove the saved decision for this workspace",
                            "Enter runs /trust clear", "clear");
  }
  if (sidebar && !sidebar->version.empty())
  {
    add_settings_row(view, "About", "AVA version", sidebar->version);
  }

  return view;
}

int run_interactive_composer(TuiRuntimeOptions options)
{
  if (!terminal_is_tty())
  {
    std::cerr << "interactive TUI requires stdin and stdout to be terminals\n";
    return 1;
  }

  clear_terminal_signal();
  auto curses = CursesSession::enter();
  if (!curses)
  {
    std::cerr << curses.error().format() << '\n';
    return 1;
  }

  ComposerSnapshot snapshot{.mode = options.mode,
                            .provider = options.provider,
                            .model = options.model,
                            .session_id = options.session_id,
                            .input = "",
                            .status = options.initial_status,
                            .context_source_count = options.context_source_count,
                            .transcript = std::move(options.initial_transcript),
                            .slash_commands = std::move(options.slash_commands),
                            .file_references = std::move(options.file_references),
                            .custom_themes = options.custom_themes,
                            .project_trust = options.project_trust};
  SidebarSnapshot sidebar{.session_id = options.session_id,
                          .mode = options.mode,
                          .provider = options.provider,
                          .model = options.model,
                          .workspace = options.workspace,
                          .git_branch = options.git_branch,
                          .version = options.app_version,
                          .context_source_count = options.context_source_count,
                          .session_path = options.session_path};
  std::vector<ava::session::ImageAttachmentRef> pending_image_attachments;
  auto refresh_token_status = [&]() {
    snapshot.token_status = options.token_status_provider ? options.token_status_provider() : std::nullopt;
    sidebar.token_status = snapshot.token_status;
  };
  auto refresh_reasoning_status = [&]() {
    snapshot.reasoning_status = options.reasoning_status_provider ? options.reasoning_status_provider() : std::nullopt;
    sidebar.reasoning_status = snapshot.reasoning_status;
  };
  auto apply_runtime_state_snapshot = [&](TuiRuntimeStateSnapshot state) {
    auto const session_changed = !state.session_id.empty() && state.session_id != snapshot.session_id;
    if (session_changed)
    {
      pending_image_attachments.clear();
      snapshot.pending_attachments.clear();
    }
    snapshot.mode = std::move(state.mode);
    snapshot.provider = std::move(state.provider);
    snapshot.model = std::move(state.model);
    snapshot.session_id = std::move(state.session_id);
    snapshot.status = std::move(state.status);
    snapshot.slash_commands = std::move(state.slash_commands);
    snapshot.file_references = std::move(state.file_references);
    snapshot.custom_themes = std::move(state.custom_themes);
    snapshot.project_trust = std::move(state.project_trust);
    snapshot.context_source_count = state.context_source_count;

    sidebar.mode = snapshot.mode;
    sidebar.provider = snapshot.provider;
    sidebar.model = snapshot.model;
    sidebar.session_id = snapshot.session_id;
    sidebar.session_path = std::move(state.session_path);
    sidebar.workspace = std::move(state.workspace);
    sidebar.git_branch = std::move(state.git_branch);
    sidebar.context_source_count = state.context_source_count;
    refresh_token_status();
    refresh_reasoning_status();
  };
  refresh_token_status();
  refresh_reasoning_status();

  bool terminal_write_failed = false;
  std::vector<std::string> input_history;
  std::optional<std::size_t> history_index;
  std::string draft_input;
  ComposerDraftState draft;
  ComposerJumpMode jump_mode = ComposerJumpMode::None;
  std::size_t selected_slash_command_index = 0;
  bool slash_palette_suppressed = false;
  bool path_completion_force_active = false;
  std::size_t transcript_scroll_offset = 0;
  std::size_t detached_new_output_count = 0;
  std::optional<SidebarSnapshot> detached_sidebar_snapshot;
  std::size_t draft_scroll_offset = 0;
  std::size_t draft_selection_anchor = std::string::npos;
  std::size_t draft_selection_cursor = std::string::npos;
  bool pending_escape_clear = false;
  ActiveSelectList active_select_list = ActiveSelectList::None;
  std::optional<PendingSessionArchiveAction> session_archive_confirmation;
  std::recursive_mutex ui_mutex;
  std::mutex prompt_request_mutex;
  std::deque<std::shared_ptr<PendingPermissionRequest>> pending_permission_requests;
  std::deque<std::shared_ptr<PendingQuestionRequest>> pending_question_requests;
  std::atomic_bool accept_prompt_requests{true};
  std::mutex prompt_audit_mutex;
  ava::app::runtime::EventSink prompt_audit_sink;

  auto emit_prompt_audit = [&](std::string status, std::string text, std::string permission_request_id = {}, std::string tool_name = {},
                               std::string reason = {}, std::string resolution_reason = {}) {
    ava::app::runtime::EventSink sink;
    {
      std::lock_guard<std::mutex> lock(prompt_audit_mutex);
      sink = prompt_audit_sink;
    }
    if (!sink)
      return;
    ava::app::runtime::Event event;
    event.type = ava::app::runtime::EventType::ProviderEvent;
    event.status = std::move(status);
    event.text = std::move(text);
    event.tool_name = std::move(tool_name);
    event.reason = std::move(reason);
    event.error_details = std::move(resolution_reason);
    if (!permission_request_id.empty())
      event.permission_request_ids.push_back(std::move(permission_request_id));
    static_cast<void>(sink(event));
  };

  auto max_draft_scroll_offset = [&](std::size_t height) {
    auto draft_snapshot = snapshot;
    draft_snapshot.input = draft.text;
    auto const main_width = composer_main_width(draft_snapshot);
    auto const composer_lines = detail::composer_block_line_count(draft_snapshot, height, main_width);
    auto const input_lines = detail::input_render_line_spans(draft.text, main_width).size();
    auto const layout = detail::composer_input_layout(input_lines, composer_lines, 0);
    return input_lines > layout.visible_input_lines ? input_lines - layout.visible_input_lines : std::size_t{0};
  };

  auto clear_draft_selection = [&]() {
    draft_selection_anchor = std::string::npos;
    draft_selection_cursor = std::string::npos;
  };

  auto draft_selection_bounds = [&]() -> std::optional<std::pair<std::size_t, std::size_t>> {
    if (draft_selection_anchor == std::string::npos || draft_selection_cursor == std::string::npos)
      return std::nullopt;
    auto start = clamp_composer_draft_cursor(draft.text, draft_selection_anchor);
    auto end = clamp_composer_draft_cursor(draft.text, draft_selection_cursor);
    if (end < start)
      std::swap(start, end);
    if (start == end)
      return std::nullopt;
    return std::pair{start, end};
  };

  auto replace_draft_selection = [&](std::string_view replacement) {
    auto const bounds = draft_selection_bounds();
    if (!bounds)
      return false;
    auto const changed = replace_composer_draft_range(draft, bounds->first, bounds->second, replacement);
    clear_draft_selection();
    return changed;
  };

  auto delete_draft_selection = [&]() { return replace_draft_selection(std::string_view{}); };

  auto selected_draft_text = [&]() -> std::optional<std::string> {
    auto const bounds = draft_selection_bounds();
    if (!bounds || bounds->first >= bounds->second || bounds->second > draft.text.size())
      return std::nullopt;
    return draft.text.substr(bounds->first, bounds->second - bounds->first);
  };

  auto copy_draft_selection = [&]() {
    auto const text = selected_draft_text();
    if (!text || text->empty())
    {
      snapshot.status = "no selection to copy";
      static_cast<void>(beep());
      return false;
    }
    auto const copied = copy_text_to_terminal_clipboard(*text);
    snapshot.status = copied ? "copied selection to clipboard" : "clipboard copy failed";
    if (!copied)
      static_cast<void>(beep());
    return copied;
  };

  auto extend_draft_selection = [&](TuiAction movement) {
    pending_escape_clear = false;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    auto const anchor = draft_selection_anchor == std::string::npos ? clamp_composer_draft_cursor(draft.text, draft.cursor) : draft_selection_anchor;
    static_cast<void>(apply_composer_draft_action(draft, movement));
    draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
    draft_selection_anchor = anchor;
    draft_selection_cursor = draft.cursor;
    snapshot.status = draft_selection_bounds() ? "selection active" : "selection boundary";
  };

  auto extend_draft_selection_to = [&](std::size_t target) {
    pending_escape_clear = false;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    auto const anchor = draft_selection_anchor == std::string::npos ? clamp_composer_draft_cursor(draft.text, draft.cursor) : draft_selection_anchor;
    draft.cursor = clamp_composer_draft_cursor(draft.text, target);
    draft.vertical_column = std::string::npos;
    draft.yank_start = std::string::npos;
    draft.yank_end = std::string::npos;
    draft_selection_anchor = anchor;
    draft_selection_cursor = draft.cursor;
    snapshot.status = draft_selection_bounds() ? "selection active" : "selection boundary";
  };

  auto extend_draft_selection_for_key = [&](Key key) -> bool {
    switch (key)
    {
      case Key::ShiftArrowUp:
        extend_draft_selection(TuiAction::CursorUp);
        return true;
      case Key::ShiftArrowDown:
        extend_draft_selection(TuiAction::CursorDown);
        return true;
      case Key::ShiftArrowLeft:
        extend_draft_selection(TuiAction::CursorLeft);
        return true;
      case Key::ShiftArrowRight:
        extend_draft_selection(TuiAction::CursorRight);
        return true;
      case Key::ShiftCtrlArrowLeft:
      case Key::ShiftAltArrowLeft:
        extend_draft_selection(TuiAction::CursorWordLeft);
        return true;
      case Key::ShiftCtrlArrowRight:
      case Key::ShiftAltArrowRight:
        extend_draft_selection(TuiAction::CursorWordRight);
        return true;
      case Key::ShiftHome:
        extend_draft_selection(TuiAction::CursorLineStart);
        return true;
      case Key::ShiftEnd:
        extend_draft_selection(TuiAction::CursorLineEnd);
        return true;
      case Key::ShiftCtrlHome:
        extend_draft_selection_to(0);
        return true;
      case Key::ShiftCtrlEnd:
        extend_draft_selection_to(draft.text.size());
        return true;
      default:
        return false;
    }
  };

  auto render = [&]() -> bool {
    if (terminal_signal_received())
      return false;
    bool wrote = false;
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      SignalBlockGuard block_signals;
      draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
      snapshot.input = draft.text;
      snapshot.input_cursor = draft.cursor;
      if (auto const selection = draft_selection_bounds())
      {
        snapshot.input_selection_start = selection->first;
        snapshot.input_selection_end = selection->second;
      }
      else
      {
        snapshot.input_selection_start = std::string::npos;
        snapshot.input_selection_end = std::string::npos;
      }
      snapshot.selected_slash_command_index = selected_slash_command_index;
      snapshot.slash_palette_suppressed = slash_palette_suppressed;
      snapshot.path_completion_force_active = path_completion_force_active;
      if (transcript_scroll_offset == 0)
      {
        detached_new_output_count = 0;
        detached_sidebar_snapshot.reset();
      }
      snapshot.sidebar = transcript_scroll_offset > 0 && detached_sidebar_snapshot ? *detached_sidebar_snapshot : sidebar;
      auto const [width, height] = terminal_size();
      snapshot.width = width;
      snapshot.height = height;
      draft_scroll_offset = std::min(draft_scroll_offset, max_draft_scroll_offset(height));
      snapshot.draft_scroll_offset = draft_scroll_offset;
      auto const main_width = composer_main_width(snapshot);
      transcript_scroll_offset = std::min(transcript_scroll_offset, max_transcript_scroll_offset_for_snapshot(snapshot, main_width, height));
      if (transcript_scroll_offset == 0)
      {
        detached_new_output_count = 0;
        detached_sidebar_snapshot.reset();
        snapshot.sidebar = sidebar;
      }
      snapshot.transcript_scroll_offset = transcript_scroll_offset;
      snapshot.transcript_new_output_count = transcript_scroll_offset > 0 ? detached_new_output_count : 0;
      wrote = draw_screen(snapshot);
    }
    return wrote && !terminal_signal_received();
  };

  auto next_display_reload_check = std::chrono::steady_clock::now() + kDisplayReloadPollInterval;
  std::optional<std::string> last_display_reload_error;
  auto maybe_reload_display_settings = [&]() -> bool {
    if (!options.on_maybe_reload_display_settings)
      return true;
    auto const now = std::chrono::steady_clock::now();
    if (now < next_display_reload_check)
      return true;
    next_display_reload_check = now + kDisplayReloadPollInterval;

    auto reloaded = options.on_maybe_reload_display_settings();
    if (!reloaded)
    {
      auto status = reloaded.error().format();
      if (last_display_reload_error && *last_display_reload_error == status)
        return true;
      last_display_reload_error = status;
      static_cast<void>(beep());
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.status = std::move(status);
      }
      return render();
    }
    last_display_reload_error.reset();
    if (!*reloaded)
      return true;

    auto state = std::move(**reloaded);
    auto status = state.status.empty() ? std::string("display theme auto-reloaded") : state.status;
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      apply_runtime_state_snapshot(std::move(state));
      if (!snapshot.processing && !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list)
      {
        push_transcript(snapshot, TranscriptItem{.label = "ava", .text = status});
        transcript_scroll_offset = 0;
      }
      snapshot.status = std::move(status);
    }
    return render();
  };

  auto render_processing_frame = [&]() -> bool {
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      if (snapshot.permission_prompt || snapshot.question_prompt)
        return true;
      ++snapshot.spinner_frame;
    }
    return render();
  };
  auto insert_newline = [&]() {
    pending_escape_clear = false;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    if (!replace_draft_selection("\n"))
      static_cast<void>(insert_composer_draft_text(draft, "\n"));
  };
  auto convert_backslash_enter_to_newline = [&]() {
    if (draft_selection_bounds())
      return false;
    if (!replace_composer_backslash_before_cursor_with_newline(draft))
      return false;
    pending_escape_clear = false;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    snapshot.status = "newline inserted";
    return true;
  };

  auto resolve_permission_prompt = [&](ava::permissions::PermissionPrompt const& prompt, std::function<bool()> const& stop_requested = {},
                                       std::function<bool()> const& request_stop = {}) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    auto permission_label = std::string("permission requested");
    if (!prompt.tool_name.empty())
      permission_label += ": " + prompt.tool_name;
    if (!prompt.command.empty())
      permission_label += " " + prompt.command;
    if (prompt.target_path.has_filename())
      permission_label += " " + prompt.target_path.generic_string();
    emit_prompt_audit("tui:permission_request", std::move(permission_label), prompt.permission_request_id, prompt.tool_name, prompt.reason);
    // A durable Deny never grants execution authority, so preserve it even
    // when one-shot Critical/unverified commands cannot be remembered as Allows.
    auto const remember_availability = permission_prompt_remember_availability(prompt, static_cast<bool>(options.remember_permission_rule));
    auto const allow_remember_available = remember_availability.allow_remember_available;
    auto const deny_remember_available = remember_availability.deny_remember_available;
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      snapshot.permission_prompt = permission_prompt_view(prompt);
      snapshot.permission_prompt->selected_choice = PermissionPromptChoice::Deny;
      snapshot.permission_prompt->allow_remember_available = allow_remember_available;
      snapshot.permission_prompt->deny_remember_available = deny_remember_available;
      snapshot.status = allow_remember_available || deny_remember_available
                            ? "permission required: A=allow once D=reject R=remember Tab/Left/Right choose Enter confirm Esc reject"
                            : "permission required: A=allow D=reject Tab/Left/Right choose Enter/Space confirm Esc reject";
    }
    static_cast<void>(beep());
    if (!render())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
    }

    auto resolve_choice = [&](PermissionPromptChoice selected) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
      auto const allow = selected == PermissionPromptChoice::Allow || selected == PermissionPromptChoice::AllowRemember;
      auto const remember = selected == PermissionPromptChoice::AllowRemember || selected == PermissionPromptChoice::DenyRemember;
      std::string remembered_rule_id;
      if (remember)
      {
        if (!options.remember_permission_rule || (allow ? !allow_remember_available : !deny_remember_available))
        {
          emit_prompt_audit("tui:permission_deny", "permission denied: remember unavailable", prompt.permission_request_id, prompt.tool_name, prompt.reason,
                            "remember unavailable");
          {
            std::lock_guard<std::recursive_mutex> lock(ui_mutex);
            snapshot.permission_prompt.reset();
            snapshot.status = "permission rule unavailable; denied";
          }
          if (!render())
          {
            return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
          }
          ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny, "permission rule storage is unavailable"};
          decision.resolution_source = "tui_remember_unavailable";
          return decision;
        }
        auto remembered =
            options.remember_permission_rule(prompt, allow ? ava::permissions::PermissionAction::Allow : ava::permissions::PermissionAction::Deny);
        if (!remembered)
        {
          auto reason = "failed to remember permission rule: " + remembered.error().format();
          emit_prompt_audit("tui:permission_deny", "permission denied: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name, prompt.reason,
                            reason);
          {
            std::lock_guard<std::recursive_mutex> lock(ui_mutex);
            snapshot.permission_prompt.reset();
            snapshot.status = "permission rule failed; denied";
          }
          if (!render())
          {
            return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
          }
          ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny, std::move(reason)};
          decision.resolution_source = "tui_remember_failed";
          return decision;
        }
        remembered_rule_id = remembered->rule_id;
      }

      if (allow)
      {
        emit_prompt_audit("tui:permission_allow", "permission allowed: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name, prompt.reason,
                          remember ? "selected allow and remember" : "selected allow");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.permission_prompt.reset();
          snapshot.status = remember ? "permission allow rule saved" : "permission allowed once";
        }
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
        }
        ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Allow};
        if (remember)
        {
          decision.reason = "remembered allow rule";
          decision.resolution_source = "persistent_rule_added";
          decision.rule_id = std::move(remembered_rule_id);
        }
        return decision;
      }
      emit_prompt_audit("tui:permission_deny", "permission denied: " + prompt.tool_name, prompt.permission_request_id, prompt.tool_name, prompt.reason,
                        remember ? "selected deny and remember" : "selected deny");
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.permission_prompt.reset();
        snapshot.status = remember ? "permission deny rule saved" : "permission denied";
      }
      if (!render())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
      }
      ava::permissions::PermissionResolutionDecision decision{ava::permissions::PermissionResolution::Deny};
      if (remember)
      {
        decision.reason = "remembered deny rule";
        decision.resolution_source = "persistent_rule_added";
        decision.rule_id = std::move(remembered_rule_id);
      }
      return decision;
    };

    while (true)
    {
      auto const choice_input = read_curses_input();
      if (stop_requested && stop_requested())
      {
        return resolve_choice(PermissionPromptChoice::Deny);
      }
      if (terminal_signal_received())
      {
        emit_prompt_audit("tui:permission_deny", "permission denied: interrupted", prompt.permission_request_id, prompt.tool_name, prompt.reason,
                          "interrupted");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.permission_prompt.reset();
          snapshot.status = "interrupted";
        }
        static_cast<void>(render());
        return ava::permissions::PermissionResolution::Deny;
      }
      if (choice_input.resize)
      {
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
        }
        continue;
      }

      if (choice_input.event.key == Key::Escape && request_stop)
      {
        static_cast<void>(request_stop());
        return resolve_choice(PermissionPromptChoice::Deny);
      }

      auto input_result = snapshot.permission_prompt ? handle_permission_prompt_input(snapshot.permission_prompt->selected_choice, choice_input.event,
                                                                                      snapshot.permission_prompt->allow_remember_available,
                                                                                      snapshot.permission_prompt->deny_remember_available)
                                                     : PermissionPromptInputResult{};
      if (input_result.action == PermissionPromptInputAction::ResolveAllow)
      {
        return resolve_choice(PermissionPromptChoice::Allow);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveDeny)
      {
        return resolve_choice(PermissionPromptChoice::Deny);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveAllowRemember)
      {
        return resolve_choice(PermissionPromptChoice::AllowRemember);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveDenyRemember)
      {
        return resolve_choice(PermissionPromptChoice::DenyRemember);
      }
      if (input_result.action == PermissionPromptInputAction::Redraw && snapshot.permission_prompt)
      {
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          if (snapshot.permission_prompt)
            snapshot.permission_prompt->selected_choice = input_result.selected_choice;
        }
        static_cast<void>(render());
        continue;
      }

      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.status =
            snapshot.permission_prompt && (snapshot.permission_prompt->allow_remember_available || snapshot.permission_prompt->deny_remember_available)
                ? "permission required: A=allow once D=reject R=remember Tab/Left/Right choose Enter confirm Esc reject"
                : "permission required: A=allow D=reject Tab/Left/Right choose Enter/Space confirm Esc reject";
      }
      if (!render())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
      }
    }
  };

  auto resolve_question_prompt = [&](ava::agent::QuestionPrompt const& prompt, std::function<bool()> const& stop_requested = {},
                                     std::function<bool()> const& request_stop = {}) -> ava::core::Result<ava::agent::QuestionAnswer> {
    static_cast<void>(request_stop);
    emit_prompt_audit("tui:question_request", prompt.question.empty() ? std::string("question requested") : "question requested: " + prompt.question);
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      snapshot.question_prompt = question_prompt_view(prompt);
      snapshot.status =
          prompt.multiple ? "question required: Space toggles, Enter sends, Esc cancels" : "question required: Enter sends, numbers choose, Esc cancels";
    }
    static_cast<void>(beep());
    if (!render())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
    }

    auto cancel_question = [&]() -> ava::core::Result<ava::agent::QuestionAnswer> {
      emit_prompt_audit("tui:question_cancel", prompt.question.empty() ? std::string("question canceled") : "question canceled: " + prompt.question);
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.question_prompt.reset();
        snapshot.status = "question canceled";
      }
      static_cast<void>(render());
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt canceled"));
    };

    auto auto_resolve_question = [&]() -> std::optional<ava::core::Result<ava::agent::QuestionAnswer>> {
      if (!prompt.auto_resolve || !prompt.auto_resolve())
        return std::nullopt;
      ava::agent::QuestionAnswer answer{.selected_options = {"done"}, .custom_text = ""};
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.question_prompt.reset();
        snapshot.status = "question answered";
      }
      if (!render())
      {
        return ava::core::Result<ava::agent::QuestionAnswer>{
            std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear question prompt"))};
      }
      emit_prompt_audit("tui:question_answer", "auto");
      return ava::core::Result<ava::agent::QuestionAnswer>{std::move(answer)};
    };

    auto read_question_input = [&]() {
      if (!prompt.auto_resolve)
        return read_curses_input();
      static_cast<void>(wtimeout(stdscr, 100));
      auto input = read_curses_input();
      static_cast<void>(wtimeout(stdscr, -1));
      return input;
    };

    auto no_input = [](CursesInput const& input) { return !input.resize && input.event.key == Key::Unknown && input.text.empty() && !input.bracketed_paste; };

    while (true)
    {
      if (auto answer = auto_resolve_question())
        return std::move(*answer);
      auto const question_input = read_question_input();
      if (auto answer = auto_resolve_question())
        return std::move(*answer);
      if (stop_requested && stop_requested())
        return cancel_question();
      if (terminal_signal_received())
      {
        emit_prompt_audit("tui:question_cancel", "question canceled: interrupted");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.question_prompt.reset();
          snapshot.status = "interrupted";
        }
        static_cast<void>(render());
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
      }
      if (question_input.resize)
      {
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }
      if (prompt.auto_resolve && no_input(question_input))
        continue;

      auto input_result =
          snapshot.question_prompt ? handle_question_prompt_input(*snapshot.question_prompt, question_input.event) : QuestionPromptInputResult{};
      if (input_result.action == QuestionPromptInputAction::Cancel)
        return cancel_question();

      if (snapshot.question_prompt && (input_result.action == QuestionPromptInputAction::Redraw || input_result.action == QuestionPromptInputAction::Copy ||
                                       input_result.action == QuestionPromptInputAction::Resolve))
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        if (snapshot.question_prompt)
        {
          snapshot.question_prompt->selected_option_index = input_result.selected_option_index;
          snapshot.question_prompt->options = std::move(input_result.options);
          snapshot.question_prompt->custom_text = std::move(input_result.custom_text);
        }
      }

      if (input_result.action == QuestionPromptInputAction::Copy)
      {
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.status = copy_text_to_terminal_clipboard(input_result.copy_text) ? "copied to clipboard" : "clipboard copy failed";
        }
        emit_prompt_audit("tui:question_copy", "question copy");
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }

      if (input_result.action == QuestionPromptInputAction::Resolve)
      {
        auto answer =
            ava::core::Result<ava::agent::QuestionAnswer>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt was dismissed"))};
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          if (snapshot.question_prompt)
            answer = question_answer_from_view(*snapshot.question_prompt);
          snapshot.question_prompt.reset();
          snapshot.status = answer ? "question answered" : "question canceled";
        }
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear question prompt"));
        }
        if (answer)
        {
          if (auto copy_text = copy_text_from_answer(*answer))
          {
            {
              std::lock_guard<std::recursive_mutex> lock(ui_mutex);
              snapshot.status = copy_text_to_terminal_clipboard(*copy_text) ? "copied to clipboard" : "clipboard copy failed";
            }
            static_cast<void>(render());
          }
          emit_prompt_audit("tui:question_answer", question_answer_audit_detail(*answer));
        }
        else
        {
          emit_prompt_audit("tui:question_cancel", "question canceled");
        }
        return answer;
      }

      if (input_result.action == QuestionPromptInputAction::Redraw)
      {
        if (!render())
        {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }

      if (!render())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
      }
    }
  };

  auto complete_permission_request = [](std::shared_ptr<PendingPermissionRequest> const& request,
                                        ava::core::Result<ava::permissions::PermissionResolutionDecision> result) {
    {
      std::lock_guard<std::mutex> lock(request->mutex);
      request->result = std::move(result);
    }
    request->ready.notify_one();
  };

  auto complete_question_request = [](std::shared_ptr<PendingQuestionRequest> const& request, ava::core::Result<ava::agent::QuestionAnswer> result) {
    {
      std::lock_guard<std::mutex> lock(request->mutex);
      request->result = std::move(result);
    }
    request->ready.notify_one();
  };

  auto fail_pending_prompt_requests = [&]() {
    accept_prompt_requests.store(false);
    std::deque<std::shared_ptr<PendingPermissionRequest>> permission_requests;
    std::deque<std::shared_ptr<PendingQuestionRequest>> question_requests;
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      permission_requests = std::move(pending_permission_requests);
      question_requests = std::move(pending_question_requests);
      pending_permission_requests.clear();
      pending_question_requests.clear();
    }
    for (auto const& permission_request : permission_requests)
    {
      complete_permission_request(permission_request, ava::permissions::PermissionResolution::Deny);
    }
    for (auto const& question_request : question_requests)
    {
      complete_question_request(question_request, std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted")));
    }
  };

  auto service_pending_prompt_request = [&](std::function<bool()> const& stop_requested = {}, std::function<bool()> const& request_stop = {}) -> bool {
    std::shared_ptr<PendingPermissionRequest> permission_request;
    std::shared_ptr<PendingQuestionRequest> question_request;
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!pending_permission_requests.empty())
      {
        permission_request = std::move(pending_permission_requests.front());
        pending_permission_requests.pop_front();
      }
      else if (!pending_question_requests.empty())
      {
        question_request = std::move(pending_question_requests.front());
        pending_question_requests.pop_front();
      }
    }
    if (permission_request)
    {
      complete_permission_request(permission_request, resolve_permission_prompt(permission_request->prompt, stop_requested, request_stop));
      return true;
    }
    if (question_request)
    {
      complete_question_request(question_request, resolve_question_prompt(question_request->prompt, stop_requested, request_stop));
      return true;
    }
    return false;
  };

  ava::permissions::PermissionResolver permission_resolver =
      [&](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    if (!accept_prompt_requests.load())
      return ava::permissions::PermissionResolution::Deny;
    auto request = std::make_shared<PendingPermissionRequest>(prompt);
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!accept_prompt_requests.load())
        return ava::permissions::PermissionResolution::Deny;
      pending_permission_requests.push_back(request);
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    request->ready.wait(lock, [&]() { return request->result.has_value() || !accept_prompt_requests.load(); });
    if (!request->result)
      return ava::permissions::PermissionResolution::Deny;
    return std::move(*request->result);
  };

  ava::agent::QuestionResolver question_resolver = [&](ava::agent::QuestionPrompt const& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
    if (!accept_prompt_requests.load())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
    }
    auto request = std::make_shared<PendingQuestionRequest>(prompt);
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!accept_prompt_requests.load())
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
      }
      pending_question_requests.push_back(request);
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    request->ready.wait(lock, [&]() { return request->result.has_value() || !accept_prompt_requests.load(); });
    if (!request->result)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
    }
    return std::move(*request->result);
  };

  auto clear_draft_for_interrupt = [&]() {
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    clear_draft_selection();
    snapshot.status.clear();
    return apply_composer_draft_action(draft, TuiAction::ClearInput);
  };

  auto open_external_editor = [&]() -> bool {
    if (!options.on_external_editor)
    {
      snapshot.status = "external editor unavailable";
      static_cast<void>(beep());
      return render();
    }
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    clear_draft_selection();
    snapshot.status = "opening external editor";
    if (!render())
      return false;

    def_prog_mode();
    endwin();
    auto edited = options.on_external_editor(draft.text);
    reset_prog_mode();
    clearok(stdscr, TRUE);
    refresh();

    if (!edited)
    {
      snapshot.status = edited.error().format();
      static_cast<void>(beep());
      return render();
    }
    if (!*edited)
    {
      snapshot.status = "external editor canceled";
      return render();
    }
    snapshot.status = replace_composer_draft(draft, std::move(**edited)) ? "external editor updated draft" : "external editor closed without changes";
    return render();
  };

  auto suspend_to_background = [&]() -> bool {
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    clear_draft_selection();

    {
      SignalBlockGuard block_signals;
      def_prog_mode();
      endwin();
      if (kill(0, SIGTSTP) != 0)
      {
        auto const saved_errno = errno;
        reset_prog_mode();
        clearok(stdscr, TRUE);
        refresh();
        snapshot.status = std::string("failed to suspend: ") + std::strerror(saved_errno);
        static_cast<void>(beep());
        return render();
      }
      reset_prog_mode();
      clearok(stdscr, TRUE);
      refresh();
    }

    snapshot.status = "resumed from background";
    return render();
  };

  auto queue_pending_image_attachment = [&](ava::session::ImageAttachmentRef const& imported, std::string label, std::string status,
                                            std::string transcript_prefix) -> bool {
    if (label.empty())
      label = imported.id;
    auto const image_capabilities = active_terminal_image_capabilities();
    auto const detail = attachment_detail(imported, image_capabilities);
    auto const preview = attachment_preview(imported, image_capabilities, options.on_load_image_attachment);
    pending_image_attachments.push_back(imported);
    snapshot.pending_attachments.push_back(PendingAttachmentItem{.label = label, .detail = detail, .preview = preview});
    snapshot.status = std::move(status);
    push_transcript(snapshot,
                    TranscriptItem{.label = "status", .text = transcript_prefix + ": " + label + " " + detail + "\nwill be sent with the next prompt"});
    transcript_scroll_offset = 0;
    return render();
  };

  auto paste_clipboard_image = [&]() -> bool {
    pending_escape_clear = false;
    jump_mode = ComposerJumpMode::None;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    path_completion_force_active = false;
    draft_scroll_offset = 0;
    clear_draft_selection();
    if (!options.on_paste_clipboard_image)
    {
      snapshot.status = "clipboard image paste unavailable";
      static_cast<void>(beep());
      return render();
    }
    snapshot.status = "reading clipboard image";
    if (!render())
      return false;

    auto imported = options.on_paste_clipboard_image();
    if (!imported)
    {
      snapshot.status = imported.error().format();
      static_cast<void>(beep());
      return render();
    }
    if (!*imported)
    {
      snapshot.status = "no clipboard image available";
      static_cast<void>(beep());
      return render();
    }
    return queue_pending_image_attachment(**imported, "clipboard image", "pasted clipboard image for next prompt", "attached clipboard image");
  };

  auto cycle_reasoning = [&]() {
    if (!options.on_cycle_reasoning)
    {
      snapshot.status = "reasoning cycling unavailable";
      return;
    }
    auto result = options.on_cycle_reasoning();
    if (!result)
    {
      snapshot.status = result.error().format();
      return;
    }
    snapshot.status = *result;
    refresh_reasoning_status();
  };

  auto toggle_thinking_visibility = [&]() {
    snapshot.thinking_visible = !snapshot.thinking_visible;
    snapshot.status = snapshot.thinking_visible ? "thinking visible" : "thinking hidden";
    transcript_scroll_offset = 0;
  };

  auto open_model_selector = [&]() -> bool {
    if (!options.model_selector_view)
    {
      snapshot.status = "model selector unavailable";
      static_cast<void>(beep());
      return true;
    }
    pending_escape_clear = false;
    session_archive_confirmation.reset();
    snapshot.select_list = options.model_selector_view();
    active_select_list = ActiveSelectList::Model;
    snapshot.status = "model selector opened";
    transcript_scroll_offset = 0;
    return render();
  };

  auto open_scoped_model_selector = [&]() -> bool {
    if (!options.scoped_model_selector_view)
    {
      snapshot.status = "scoped model selector unavailable";
      static_cast<void>(beep());
      return true;
    }
    pending_escape_clear = false;
    session_archive_confirmation.reset();
    snapshot.select_list = options.scoped_model_selector_view();
    active_select_list = ActiveSelectList::ScopedModels;
    snapshot.status = "scoped model selector opened";
    transcript_scroll_offset = 0;
    return render();
  };

  auto open_session_selector = [&]() -> bool {
    if (!options.session_selector_view)
    {
      snapshot.status = "session selector unavailable";
      static_cast<void>(beep());
      return true;
    }
    pending_escape_clear = false;
    session_archive_confirmation.reset();
    snapshot.select_list = options.session_selector_view();
    active_select_list = ActiveSelectList::Session;
    snapshot.status = "session selector opened";
    transcript_scroll_offset = 0;
    return render();
  };

  auto cycle_model = [&](bool forward) {
    if (!options.on_cycle_model)
    {
      snapshot.status = "model cycling unavailable";
      static_cast<void>(beep());
      return;
    }
    auto result = options.on_cycle_model(forward);
    if (!result)
    {
      snapshot.status = result.error().format();
      static_cast<void>(beep());
      return;
    }
    apply_runtime_state_snapshot(std::move(*result));
    transcript_scroll_offset = 0;
  };

  auto slash_palette_active = [&]() { return !slash_palette_suppressed && slash_palette_visible(draft.text, draft.cursor, snapshot.slash_commands); };
  auto file_reference_palette_active = [&]() {
    return !slash_palette_suppressed && !slash_palette_active() && file_reference_palette_visible(draft.text, draft.cursor, snapshot.file_references);
  };
  auto path_completion_palette_active = [&]() {
    return !slash_palette_suppressed && !slash_palette_active() && !file_reference_palette_active() &&
           path_completion_palette_visible(draft.text, draft.cursor, snapshot.file_references, path_completion_force_active);
  };

  auto scroll_up = [&](std::size_t amount) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const max_scroll = max_transcript_scroll_offset_for_snapshot(snapshot, composer_main_width(snapshot), height);
    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    transcript_scroll_offset = std::min(max_scroll, clamped_scroll + amount);
    if (transcript_scroll_offset > 0 && !detached_sidebar_snapshot)
      detached_sidebar_snapshot = sidebar;
  };

  auto scroll_down = [&](std::size_t amount) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const max_scroll = max_transcript_scroll_offset_for_snapshot(snapshot, composer_main_width(snapshot), height);
    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    transcript_scroll_offset = amount >= clamped_scroll ? 0 : clamped_scroll - amount;
    if (transcript_scroll_offset == 0)
    {
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
    }
  };

  auto jump_to_bottom = [&](std::string status) {
    pending_escape_clear = false;
    transcript_scroll_offset = 0;
    detached_new_output_count = 0;
    detached_sidebar_snapshot.reset();
    snapshot.status = std::move(status);
  };

  auto scroll_to_message_boundary = [&](bool previous) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const main_width = composer_main_width(snapshot);
    auto const transcript_height = transcript_height_for_snapshot(snapshot, main_width, height);
    auto const rendered_transcript = detail::render_transcript_lines(snapshot.transcript, main_width, snapshot.tool_details_visible, snapshot.thinking_visible);
    if (transcript_height == 0 || rendered_transcript.size() <= transcript_height)
    {
      transcript_scroll_offset = 0;
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
      snapshot.status = "transcript fits on screen";
      return;
    }

    auto const max_scroll = rendered_transcript.size() - transcript_height;
    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    auto const current_start = max_scroll - clamped_scroll;
    auto const starts = detail::transcript_message_start_lines(snapshot.transcript, main_width, snapshot.tool_details_visible, snapshot.thinking_visible);
    if (starts.empty())
    {
      snapshot.status = "no message boundaries";
      return;
    }

    auto target_start = std::optional<std::size_t>{};
    if (previous)
    {
      for (auto const start : starts)
      {
        if (start >= current_start)
          break;
        target_start = start;
      }
      if (!target_start)
      {
        snapshot.status = "oldest message visible";
        return;
      }
    }
    else
    {
      for (auto const start : starts)
      {
        if (start > current_start)
        {
          target_start = start;
          break;
        }
      }
      if (!target_start || *target_start >= max_scroll)
      {
        jump_to_bottom("live tail");
        return;
      }
    }

    transcript_scroll_offset = max_scroll > *target_start ? max_scroll - *target_start : 0;
    if (transcript_scroll_offset > 0 && !detached_sidebar_snapshot)
      detached_sidebar_snapshot = sidebar;
    if (transcript_scroll_offset == 0)
    {
      detached_new_output_count = 0;
      detached_sidebar_snapshot.reset();
    }
    snapshot.status = previous ? "previous message" : "next message";
  };

  enum class InputLoopAction
  {
    None,
    ContinueLoop,
    BreakLoop
  };
  std::optional<std::string> forced_slash_submission;
  auto handle_submit = [&]() -> InputLoopAction {
    pending_escape_clear = false;
    std::optional<std::string> immediate_slash_submission;
    if (forced_slash_submission)
    {
      immediate_slash_submission = std::move(forced_slash_submission);
      forced_slash_submission.reset();
    }
    else if (((exact_command(draft.text, "/models") || exact_command(draft.text, "/model")) && options.model_selector_view) ||
             (exact_command(draft.text, "/scoped-models") && options.scoped_model_selector_view) ||
             ((exact_command(draft.text, "/sessions") || exact_command(draft.text, "/tree") || exact_command(draft.text, "/resume")) &&
              options.session_selector_view))
    {
      immediate_slash_submission = expanded_composer_draft_text(draft);
    }
    else if (!slash_palette_suppressed && slash_palette_visible(draft.text, draft.cursor, snapshot.slash_commands))
    {
      auto const matches = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands);
      if (!matches.empty())
      {
        selected_slash_command_index = clamp_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        if (auto const disabled_reason =
                slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index))
        {
          snapshot.status = "command disabled: " + *disabled_reason;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto const& selected_item = matches[selected_slash_command_index];
        auto const selection_text = slash_command_selection_text(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        if (!selected_item.argument_completion && selected_item.command == "/connect")
        {
          immediate_slash_submission = selection_text.text;
        }
        else
        {
          clear_draft_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(selection_text.text), selection_text.cursor));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          draft_scroll_offset = 0;
          history_index.reset();
          draft_input.clear();
          snapshot.status = "command selected - press Enter to run";
          snapshot.selected_slash_command_index = selected_slash_command_index;
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        selected_slash_command_index = 0;
        path_completion_force_active = false;
        snapshot.selected_slash_command_index = selected_slash_command_index;
      }
    }
    if (file_reference_palette_active())
    {
      selected_slash_command_index = clamp_file_reference_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
      auto selection = file_reference_selection_text(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
      clear_draft_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "file reference selected";
      snapshot.selected_slash_command_index = selected_slash_command_index;
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      return InputLoopAction::ContinueLoop;
    }
    if (path_completion_palette_active())
    {
      selected_slash_command_index =
          clamp_path_completion_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index, path_completion_force_active);
      auto selection =
          path_completion_selection_text(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index, path_completion_force_active);
      clear_draft_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "path selected";
      snapshot.selected_slash_command_index = selected_slash_command_index;
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      return InputLoopAction::ContinueLoop;
    }
    auto const submitted = immediate_slash_submission ? *immediate_slash_submission : expanded_composer_draft_text(draft);
    clear_draft_selection();
    reset_composer_draft(draft);
    jump_mode = ComposerJumpMode::None;
    draft_scroll_offset = 0;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    path_completion_force_active = false;
    if (!submitted.empty())
    {
      if ((exact_command(submitted, "/models") || exact_command(submitted, "/model")) && options.model_selector_view)
      {
        push_history(input_history, submitted);
        if (!open_model_selector())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (exact_command(submitted, "/scoped-models") && options.scoped_model_selector_view)
      {
        push_history(input_history, submitted);
        if (!open_scoped_model_selector())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if ((exact_command(submitted, "/sessions") || exact_command(submitted, "/tree") || exact_command(submitted, "/resume")) && options.session_selector_view)
      {
        push_history(input_history, submitted);
        if (!open_session_selector())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto reload_target = reload_command_argument(submitted))
      {
        push_history(input_history, submitted);
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        auto const parsed_reload_target = reload_target_from_argument(*reload_target);
        if (!parsed_reload_target)
        {
          snapshot.status = "invalid_argument: unsupported reload target\n  target: " + *reload_target + "\n  supported: keybindings, theme";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        if (*parsed_reload_target == ReloadTarget::DisplaySettings)
        {
          if (!options.on_reload_display_settings)
          {
            snapshot.status = "display reload unavailable";
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
            static_cast<void>(beep());
            if (!render())
            {
              terminal_write_failed = true;
              return InputLoopAction::BreakLoop;
            }
            return InputLoopAction::ContinueLoop;
          }
          auto reloaded = options.on_reload_display_settings();
          if (!reloaded)
          {
            snapshot.status = reloaded.error().format();
            push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
            transcript_scroll_offset = 0;
            static_cast<void>(beep());
            if (!render())
            {
              terminal_write_failed = true;
              return InputLoopAction::BreakLoop;
            }
            return InputLoopAction::ContinueLoop;
          }
          auto status = reloaded->status.empty() ? std::string("display theme reloaded") : reloaded->status;
          apply_runtime_state_snapshot(std::move(*reloaded));
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = std::move(status), .meta = assistant_meta_for_snapshot(snapshot)});
          transcript_scroll_offset = 0;
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        if (!options.on_reload_key_bindings)
        {
          snapshot.status = "reload unavailable";
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto reloaded = options.on_reload_key_bindings();
        if (!reloaded)
        {
          snapshot.status = reloaded.error().format();
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        options.key_bindings = std::move(reloaded->key_bindings);
        apply_runtime_state_snapshot(std::move(reloaded->state));
        push_transcript(snapshot, TranscriptItem{.label = "ava", .text = "keybindings reloaded", .meta = assistant_meta_for_snapshot(snapshot)});
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/hotkeys" || submitted == "/keybindings")
      {
        push_history(input_history, submitted);
        snapshot.select_list = hotkeys_select_list_view(options.key_bindings);
        active_select_list = ActiveSelectList::Hotkeys;
        snapshot.status = "keybindings opened";
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/settings")
      {
        push_history(input_history, submitted);
        refresh_token_status();
        refresh_reasoning_status();
        auto settings_snapshot = snapshot;
        settings_snapshot.sidebar = sidebar;
        snapshot.select_list = settings_select_list_view(settings_snapshot, options.key_bindings);
        active_select_list = ActiveSelectList::Settings;
        snapshot.status = "settings opened";
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto attach_target = attach_command_argument(submitted))
      {
        push_history(input_history, submitted);
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        if (attach_target->empty())
        {
          snapshot.status = "invalid_argument: usage: /attach <image-path>";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        if (!options.on_attach_image)
        {
          snapshot.status = "image attachment import unavailable";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto imported = options.on_attach_image(*attach_target);
        if (!imported)
        {
          snapshot.status = imported.error().format();
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          transcript_scroll_offset = 0;
          static_cast<void>(beep());
          if (!render())
          {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        auto label = compact_path_leaf(*attach_target);
        if (!queue_pending_image_attachment(*imported, std::move(label), "attached image for next prompt", "attached image"))
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto tool_query = tool_command_argument(submitted))
      {
        push_history(input_history, submitted);
        auto tool_card = latest_tool_detail_card(snapshot.transcript, *tool_query);
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        if (tool_card)
        {
          auto status = tool_query->empty() ? std::string("showing latest tool details") : std::string("showing matching tool details");
          push_transcript(snapshot, TranscriptItem{.label = "status", .text = status});
          push_transcript(snapshot, TranscriptItem{.tool = std::move(*tool_card)});
          snapshot.status = std::move(status);
        }
        else
        {
          snapshot.status = tool_query->empty() ? "no tool details to show" : "no matching tool details to show";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          static_cast<void>(beep());
        }
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto diff_query = diff_command_argument(submitted))
      {
        push_history(input_history, submitted);
        auto diff_text = latest_tool_diff_copy_text(snapshot.transcript, *diff_query);
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        if (diff_text)
        {
          auto const title = diff_query->empty() ? std::string_view("Latest tool diff:") : std::string_view("Matching tool diff:");
          push_transcript(snapshot,
                          TranscriptItem{.label = "ava", .text = diff_transcript_text(title, *diff_text), .meta = assistant_meta_for_snapshot(snapshot)});
          snapshot.status = diff_query->empty() ? "showing latest tool diff" : "showing matching tool diff";
        }
        else
        {
          snapshot.status = diff_query->empty() ? "no tool diff to show" : "no matching tool diff to show";
          push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
          static_cast<void>(beep());
        }
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (auto copy_target = copy_command_argument(submitted))
      {
        push_history(input_history, submitted);
        auto const target = parse_copy_target(*copy_target);
        std::optional<std::string> copy_text;
        std::string copied_status;
        std::string missing_status;
        bool valid_target = true;
        bool copied = false;
        if (target.name.empty())
        {
          copy_text = latest_ava_message_copy_text(snapshot.transcript);
          copied_status = "copied last AVA message to clipboard";
          missing_status = "no AVA messages to copy";
        }
        else if (target.name == "tool" || target.name == "tools")
        {
          copy_text = latest_tool_copy_text(snapshot.transcript, target.query);
          copied_status = target.query.empty() ? "copied latest tool details to clipboard" : "copied matching tool details to clipboard";
          missing_status = target.query.empty() ? "no tool details to copy" : "no matching tool details to copy";
        }
        else if (target.name == "diff" || target.name == "diffs")
        {
          copy_text = latest_tool_diff_copy_text(snapshot.transcript, target.query);
          copied_status = target.query.empty() ? "copied latest tool diff to clipboard" : "copied matching tool diff to clipboard";
          missing_status = target.query.empty() ? "no tool diff to copy" : "no matching tool diff to copy";
        }
        else if (target.name == "permission" || target.name == "permissions")
        {
          copy_text = latest_permission_copy_text(snapshot.transcript, target.query);
          copied_status = target.query.empty() ? "copied latest permission details to clipboard" : "copied matching permission details to clipboard";
          missing_status = target.query.empty() ? "no permission details to copy" : "no matching permission details to copy";
        }
        else
        {
          valid_target = false;
          snapshot.status =
              "invalid_argument: unsupported copy target\n  target: " + target.name + "\n  supported: tool [query], diff [query], permission [query]";
        }

        if (valid_target)
        {
          copied = copy_text && copy_text_to_terminal_clipboard(*copy_text);
          if (copied)
          {
            snapshot.status = std::move(copied_status);
          }
          else
          {
            snapshot.status = copy_text ? "clipboard copy failed" : std::move(missing_status);
          }
        }
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = copied ? "status" : "error", .text = snapshot.status});
        transcript_scroll_offset = 0;
        if (!copied)
          static_cast<void>(beep());
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/details")
      {
        push_history(input_history, submitted);
        snapshot.tool_details_visible = !snapshot.tool_details_visible;
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = "ava",
                                                 .text = snapshot.tool_details_visible ? "tool details are now visible" : "tool details are now compact",
                                                 .meta = assistant_meta_for_snapshot(snapshot)});
        snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
        transcript_scroll_offset = 0;
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/thinking")
      {
        push_history(input_history, submitted);
        toggle_thinking_visibility();
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = "ava",
                                                 .text = snapshot.thinking_visible ? "thinking blocks are now visible" : "thinking blocks are now hidden",
                                                 .meta = assistant_meta_for_snapshot(snapshot)});
        if (!render())
        {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      auto const is_command_submission = submitted.starts_with('/') || shell_helper_submission(submitted);
      if (is_command_submission && session_switching_command(submitted) && !pending_image_attachments.empty())
      {
        pending_image_attachments.clear();
        snapshot.pending_attachments.clear();
      }
      auto const supports_active_queue = !is_command_submission || is_compact_command(submitted);
      auto const transcript_before_submit = snapshot.transcript;
      auto submitted_transcript = transcript_before_submit;
      auto submit_image_attachments = std::vector<ava::session::ImageAttachmentRef>{};
      if (!is_command_submission && !pending_image_attachments.empty())
      {
        submit_image_attachments = std::move(pending_image_attachments);
        pending_image_attachments.clear();
        snapshot.pending_attachments.clear();
      }
      ava::app::EventBus event_bus;
      EventEnvelopeQueue event_queue;
      TuiEventState event_state;
      std::atomic_bool run_cancel_requested{false};
      bool close_after_submit = false;
      auto cancel_requested = [&run_cancel_requested]() { return run_cancel_requested.load(); };
      event_bus.subscribe(event_queue.sink());
      std::optional<TuiActiveRunQueues> active_queues;
      std::mutex event_context_mutex;
      std::string current_request_id;
      auto set_current_request_id = [&](std::string request_id) {
        std::lock_guard lock(event_context_mutex);
        current_request_id = std::move(request_id);
      };
      if (supports_active_queue && options.create_active_run_queues)
      {
        active_queues = options.create_active_run_queues([&event_bus](ava::app::EventEnvelope const& event) { return event_bus.publish(event); });
        if (!active_queues->active_request_id.empty())
        {
          set_current_request_id(active_queues->active_request_id);
          auto mark_follow_up_started = active_queues->mark_follow_up_started;
          active_queues->mark_follow_up_started = [&, mark_follow_up_started](TuiQueuedFollowUp const& follow_up) {
            set_current_request_id(follow_up.request_id);
            if (mark_follow_up_started)
              return mark_follow_up_started(follow_up);
            return ava::core::VoidResult{};
          };
        }
      }
      auto runtime_event_to_bus_sink = [&]() -> ava::app::runtime::EventSink {
        return [&](ava::app::runtime::Event const& event) {
          ava::app::EventEnvelopeContext event_context;
          {
            std::lock_guard lock(event_context_mutex);
            if (!current_request_id.empty())
            {
              event_context.request_id = current_request_id;
              event_context.correlation_id = current_request_id;
            }
          }
          return event_bus.publish(ava::app::to_event_envelope(event, event_context));
        };
      };
      auto event_sink = supports_active_queue ? runtime_event_to_bus_sink() : ava::app::runtime::EventSink{};
      {
        std::lock_guard<std::mutex> lock(prompt_audit_mutex);
        prompt_audit_sink = event_sink;
      }
      auto const turn_started_at = std::chrono::steady_clock::now();
      auto upsert_stopping_activity = [&]() {
        auto item = SidebarActivityItem{.id = "stopping",
                                        .label = "stopping",
                                        .detail = "waiting for active work to stop; queued drafts skip on stop",
                                        .status = ToolTimelineStatus::Running};
        auto existing = std::ranges::find_if(sidebar.activity, [&](SidebarActivityItem const& activity) { return activity.id == item.id; });
        if (existing == sidebar.activity.end())
        {
          sidebar.activity.push_back(std::move(item));
        }
        else
        {
          *existing = std::move(item);
        }
      };
      auto settle_turn_activity = [&]() {
        auto responding = std::ranges::find_if(sidebar.activity, [](SidebarActivityItem const& activity) { return activity.id == "responding"; });
        if (responding == sidebar.activity.end() || responding->status != ToolTimelineStatus::Running)
          return;
        if (run_cancel_requested.load() || event_state.run_status == TuiEventRunStatus::Canceled)
        {
          responding->status = ToolTimelineStatus::Canceled;
          responding->detail = "assistant stopped";
          return;
        }
        if (event_state.run_status == TuiEventRunStatus::Error)
        {
          responding->status = ToolTimelineStatus::Error;
          responding->detail = "assistant failed";
          return;
        }
        responding->status = ToolTimelineStatus::Success;
        responding->detail = "assistant responded";
      };
      auto request_stop = [&]() -> bool {
        bool const was_already_requested = run_cancel_requested.exchange(true);
        fail_pending_prompt_requests();
        if (!was_already_requested)
          static_cast<void>(beep());
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.status = "stop requested";
          upsert_stopping_activity();
        }
        return render();
      };
      auto request_close_after_submit = [&]() {
        run_cancel_requested.store(true);
        close_after_submit = true;
        fail_pending_prompt_requests();
      };
      auto drain_runtime_events = [&]() -> RuntimeEventDrainResult {
        auto events = event_queue.drain();
        if (events.empty())
          return RuntimeEventDrainResult::NoEvents;
        for (auto const& event : events)
        {
          apply_event_envelope(event_state, event);
        }
        auto turn_transcript = event_state_transcript_snapshot(event_state);
        auto detached_indicator_changed = false;
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          auto const preserve_viewport = transcript_scroll_offset > 0;
          auto const old_rendered_lines = preserve_viewport ? detail::render_transcript_lines(snapshot.transcript, composer_main_width(snapshot),
                                                                                              snapshot.tool_details_visible, snapshot.thinking_visible)
                                                                  .size()
                                                            : std::size_t{0};
          if (preserve_viewport && !detached_sidebar_snapshot)
            detached_sidebar_snapshot = sidebar;
          apply_assistant_turn_meta(turn_transcript, assistant_meta_for_snapshot(snapshot, std::chrono::steady_clock::now() - turn_started_at));
          snapshot.transcript = submitted_transcript;
          snapshot.transcript.insert(snapshot.transcript.end(), turn_transcript.begin(), turn_transcript.end());
          truncate_transcript(snapshot.transcript);
          snapshot.queued_messages = event_state.queued_messages;
          sidebar.activity = event_state.activity;
          if (run_cancel_requested.load() && event_state.run_status == TuiEventRunStatus::Running)
          {
            upsert_stopping_activity();
          }
          for (auto const& file : event_state.modified_files)
          {
            auto const exists = std::ranges::any_of(sidebar.modified_files, [&](SidebarModifiedFile const& existing) { return existing.path == file.path; });
            if (!exists)
              sidebar.modified_files.push_back(file);
          }
          constexpr auto kMaxSidebarModifiedFiles = std::size_t{50};
          if (sidebar.modified_files.size() > kMaxSidebarModifiedFiles)
          {
            sidebar.modified_files.erase(
                sidebar.modified_files.begin(),
                sidebar.modified_files.begin() + static_cast<std::ptrdiff_t>(sidebar.modified_files.size() - kMaxSidebarModifiedFiles));
          }
          if (preserve_viewport)
          {
            auto const new_rendered_lines =
                detail::render_transcript_lines(snapshot.transcript, composer_main_width(snapshot), snapshot.tool_details_visible, snapshot.thinking_visible)
                    .size();
            if (new_rendered_lines > old_rendered_lines)
            {
              auto const added_lines = new_rendered_lines - old_rendered_lines;
              detached_new_output_count += added_lines;
              detached_indicator_changed = true;
              transcript_scroll_offset += new_rendered_lines - old_rendered_lines;
            }
            else
            {
              ++detached_new_output_count;
              detached_indicator_changed = true;
            }
          }
          else
          {
            transcript_scroll_offset = 0;
          }
          if (transcript_scroll_offset > 0)
          {
            auto const [width, height] = terminal_size();
            snapshot.width = width;
            snapshot.height = height;
            auto const main_width = composer_main_width(snapshot);
            transcript_scroll_offset = std::min(transcript_scroll_offset, max_transcript_scroll_offset_for_snapshot(snapshot, main_width, height));
            snapshot.transcript_scroll_offset = transcript_scroll_offset;
          }
        }
        if (transcript_scroll_offset > 0 && !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list)
        {
          if (!detached_indicator_changed)
            return RuntimeEventDrainResult::UpdatedNoRender;
          return render() ? RuntimeEventDrainResult::Rendered : RuntimeEventDrainResult::RenderFailed;
        }
        return render() ? RuntimeEventDrainResult::Rendered : RuntimeEventDrainResult::RenderFailed;
      };

      if (is_command_submission && should_echo_slash_command(submitted))
      {
        auto command_item = TranscriptItem{.label = "cmd", .text = submitted};
        submitted_transcript.push_back(command_item);
        push_transcript(snapshot, std::move(command_item));
      }
      push_history(input_history, submitted);
      snapshot.status = is_command_submission ? "running command..." : "thinking...";
      snapshot.processing = true;
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      auto result = TuiSubmitResult{};
      if (options.on_submit)
      {
        accept_prompt_requests.store(true);
        auto submit_future = std::async(std::launch::async, [&]() {
          auto take_steering_messages = active_queues ? active_queues->take_steering_messages : std::function<ava::core::Result<std::vector<std::string>>()>{};
          auto skip_active_steering = active_queues ? active_queues->skip_active_steering : std::function<ava::core::VoidResult(std::string_view)>{};
          auto take_next_follow_up = active_queues ? active_queues->take_next_follow_up : std::function<std::optional<TuiQueuedFollowUp>()>{};
          auto mark_follow_up_started =
              active_queues ? active_queues->mark_follow_up_started : std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)>{};
          return options.on_submit(submitted, TuiSubmitContext{.permission_resolver = permission_resolver,
                                                               .question_resolver = question_resolver,
                                                               .event_sink = event_sink,
                                                               .cancel_requested = cancel_requested,
                                                               .take_steering_messages = take_steering_messages,
                                                               .skip_active_steering = skip_active_steering,
                                                               .take_next_follow_up = take_next_follow_up,
                                                               .mark_follow_up_started = mark_follow_up_started,
                                                               .image_attachments = submit_image_attachments});
        });
        auto handle_active_input = [&](CursesInput const& active_input) -> bool {
          if (active_input.resize)
            return render();

          auto const active_event = active_input.event;
          auto active_is_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, active_event.key); };
          auto restore_latest_queued_message = [&]() {
            if (!active_queues || !active_queues->restore_latest)
            {
              snapshot.status = "active-run restore unavailable";
              return render();
            }
            auto restored = active_queues->restore_latest();
            if (!restored)
            {
              snapshot.status = restored.error().format();
              static_cast<void>(beep());
              return render();
            }
            auto const restored_text = restored->steering ? "/steer " + restored->message : restored->message;
            clear_draft_selection();
            static_cast<void>(replace_composer_draft(draft, restored_text));
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            snapshot.status = restored->steering ? "steering restored" : "follow-up restored";
            return render();
          };
          auto queue_active_draft = [&](bool follow_up_only) {
            if (draft.text.empty())
            {
              snapshot.status = "type a follow-up before queueing";
              return render();
            }
            if (!follow_up_only && draft.text == "/restore")
            {
              return restore_latest_queued_message();
            }
            auto const steering_prefix = std::string_view("/steer ");
            bool const steering_draft = !follow_up_only && draft.text.starts_with(steering_prefix);
            if ((draft.text.starts_with('/') || shell_helper_submission(draft.text)) && !steering_draft)
            {
              snapshot.status = "commands run between turns";
              return render();
            }
            if (run_cancel_requested.load())
            {
              snapshot.status = "stop requested; queueing disabled";
              return render();
            }
            if (!active_queues)
            {
              snapshot.status = "active-run queue unavailable";
              return render();
            }
            if (steering_draft && !active_queues->queue_steering)
            {
              snapshot.status = "active-run steering unavailable";
              return render();
            }
            if (!steering_draft && !active_queues->queue_follow_up)
            {
              snapshot.status = "active-run follow-up unavailable";
              return render();
            }

            auto queued_text = expanded_composer_draft_text(draft);
            auto queued =
                steering_draft ? active_queues->queue_steering(queued_text.substr(steering_prefix.size())) : active_queues->queue_follow_up(queued_text);
            if (!queued)
            {
              snapshot.status = queued.error().format();
              static_cast<void>(beep());
              return render();
            }
            push_history(input_history, queued_text);
            clear_draft_selection();
            reset_composer_draft(draft);
            jump_mode = ComposerJumpMode::None;
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            snapshot.status = steering_draft ? "steering queued" : "follow-up queued";
            return render();
          };
          if (jump_mode != ComposerJumpMode::None)
          {
            if (active_is_action(TuiAction::JumpForward) || active_is_action(TuiAction::JumpBackward))
            {
              jump_mode = ComposerJumpMode::None;
              snapshot.status = "jump cancelled";
              return render();
            }
            if (auto const target = printable_jump_target(active_input))
            {
              bool const forward = jump_mode == ComposerJumpMode::Forward;
              jump_mode = ComposerJumpMode::None;
              pending_escape_clear = false;
              history_index.reset();
              draft_input.clear();
              selected_slash_command_index = 0;
              slash_palette_suppressed = false;
              path_completion_force_active = false;
              draft_scroll_offset = 0;
              clear_draft_selection();
              snapshot.status =
                  jump_composer_draft_to_character(draft, *target, forward) ? (forward ? "jumped forward" : "jumped backward") : "jump character not found";
              return render();
            }
            jump_mode = ComposerJumpMode::None;
          }
          if (active_event.key == Key::Escape || active_is_action(TuiAction::Cancel))
          {
            return request_stop();
          }
          if (active_is_action(TuiAction::CopySelection) && draft_selection_bounds())
          {
            pending_escape_clear = false;
            static_cast<void>(copy_draft_selection());
            return render();
          }
          if (active_is_action(TuiAction::Interrupt))
          {
            if (!draft.text.empty())
            {
              static_cast<void>(clear_draft_for_interrupt());
              snapshot.selected_slash_command_index = selected_slash_command_index;
              return render();
            }
            request_close_after_submit();
            return true;
          }
          bool const active_ctrl_d_delete_forward = active_event.key == Key::CtrlD && active_is_action(TuiAction::DeleteForward) && !draft.text.empty();
          if (active_is_action(TuiAction::Exit) && !active_ctrl_d_delete_forward)
          {
            request_close_after_submit();
            return true;
          }
          auto insert_active_text = [&]() {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            draft_scroll_offset = 0;
            auto const text = active_input.text.empty() ? std::string(1, active_event.character) : active_input.text;
            if (active_input.bracketed_paste)
            {
              static_cast<void>(delete_draft_selection());
              static_cast<void>(insert_composer_paste_text(draft, text));
              snapshot.status = "pasted into draft safely";
            }
            else
            {
              if (!replace_draft_selection(text))
                static_cast<void>(insert_composer_draft_text(draft, text));
            }
          };
          if (active_event.key == Key::Character)
          {
            insert_active_text();
            return render();
          }
          if (extend_draft_selection_for_key(active_event.key))
          {
            return render();
          }
          if (active_event.key == Key::CtrlHome || active_event.key == Key::CtrlEnd)
          {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            clear_draft_selection();
            draft.cursor = active_event.key == Key::CtrlHome ? 0 : draft.text.size();
            draft.vertical_column = std::string::npos;
            draft.yank_start = std::string::npos;
            draft.yank_end = std::string::npos;
            return render();
          }
          if (active_is_action(TuiAction::AutocompleteAccept) && slash_palette_active())
          {
            selected_slash_command_index = clamp_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
            if (auto const disabled_reason =
                    slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index))
            {
              snapshot.status = "command disabled: " + *disabled_reason;
              static_cast<void>(beep());
            }
            else
            {
              clear_draft_selection();
              auto selection = slash_command_selection_text(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
              static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
              selected_slash_command_index = 0;
              path_completion_force_active = false;
              draft_scroll_offset = 0;
              history_index.reset();
              draft_input.clear();
              snapshot.status = "command selected - press Enter to queue";
            }
            return render();
          }
          if (active_is_action(TuiAction::AutocompleteAccept) && file_reference_palette_active())
          {
            selected_slash_command_index = clamp_file_reference_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
            auto selection = file_reference_selection_text(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
            clear_draft_selection();
            static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
            selected_slash_command_index = 0;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            snapshot.status = "file reference selected";
            return render();
          }
          if (active_is_action(TuiAction::AutocompleteAccept) && path_completion_palette_active())
          {
            selected_slash_command_index =
                clamp_path_completion_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index, path_completion_force_active);
            auto selection =
                path_completion_selection_text(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index, path_completion_force_active);
            clear_draft_selection();
            static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
            selected_slash_command_index = 0;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            snapshot.status = "path selected";
            return render();
          }
          if (active_is_action(TuiAction::NewLine))
          {
            insert_newline();
            return render();
          }
          if (active_is_action(TuiAction::ExternalEditor))
          {
            return open_external_editor();
          }
          if (active_is_action(TuiAction::Suspend))
          {
            return suspend_to_background();
          }
          if (active_is_action(TuiAction::ClipboardPasteImage))
          {
            return paste_clipboard_image();
          }
          if (active_is_action(TuiAction::MessageDequeue))
          {
            return restore_latest_queued_message();
          }
          if (active_is_action(TuiAction::JumpForward) || active_is_action(TuiAction::JumpBackward))
          {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            jump_mode = active_is_action(TuiAction::JumpForward) ? ComposerJumpMode::Forward : ComposerJumpMode::Backward;
            snapshot.status = active_is_action(TuiAction::JumpForward) ? "jump forward: type character" : "jump backward: type character";
            return render();
          }
          if (active_is_action(TuiAction::MessageFollowUp))
          {
            return queue_active_draft(true);
          }
          if (active_is_action(TuiAction::Submit))
          {
            if (active_event.key == Key::Enter && convert_backslash_enter_to_newline())
              return render();
            return queue_active_draft(false);
          }
          bool const active_delete_forward = active_is_action(TuiAction::DeleteForward) && (active_event.key != Key::CtrlD || active_ctrl_d_delete_forward);
          if (active_is_action(TuiAction::DeleteBackward) || active_delete_forward || active_is_action(TuiAction::DeleteWordBackward) ||
              active_is_action(TuiAction::DeleteWordForward) || active_is_action(TuiAction::DeleteToLineStart) ||
              active_is_action(TuiAction::DeleteToLineEnd) || active_is_action(TuiAction::ClearInput) || active_is_action(TuiAction::CursorLeft) ||
              active_is_action(TuiAction::CursorRight) || active_is_action(TuiAction::CursorLineStart) || active_is_action(TuiAction::CursorLineEnd) ||
              active_is_action(TuiAction::CursorWordLeft) || active_is_action(TuiAction::CursorWordRight) || active_is_action(TuiAction::Undo) ||
              active_is_action(TuiAction::Redo) || active_is_action(TuiAction::Yank) || active_is_action(TuiAction::YankPop))
          {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            draft_scroll_offset = 0;
            if (active_is_action(TuiAction::DeleteBackward))
            {
              if (!delete_draft_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteBackward));
            }
            else if (active_delete_forward)
            {
              if (!delete_draft_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteForward));
            }
            else if (active_is_action(TuiAction::DeleteWordBackward))
            {
              if (!delete_draft_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordBackward));
            }
            else if (active_is_action(TuiAction::DeleteWordForward))
            {
              if (!delete_draft_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordForward));
            }
            else if (active_is_action(TuiAction::DeleteToLineStart))
            {
              if (!delete_draft_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineStart));
            }
            else if (active_is_action(TuiAction::DeleteToLineEnd))
            {
              if (!delete_draft_selection())
                static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineEnd));
            }
            else if (active_is_action(TuiAction::ClearInput))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::ClearInput));
            }
            else if (active_is_action(TuiAction::CursorLeft))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLeft));
            }
            else if (active_is_action(TuiAction::CursorRight))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorRight));
            }
            else if (active_is_action(TuiAction::CursorLineStart))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineStart));
            }
            else if (active_is_action(TuiAction::CursorLineEnd))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineEnd));
            }
            else if (active_is_action(TuiAction::CursorWordLeft))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordLeft));
            }
            else if (active_is_action(TuiAction::CursorWordRight))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordRight));
            }
            else if (active_is_action(TuiAction::Undo))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Undo));
            }
            else if (active_is_action(TuiAction::Redo))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Redo));
            }
            else if (active_is_action(TuiAction::Yank))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Yank));
            }
            else if (active_is_action(TuiAction::YankPop))
            {
              clear_draft_selection();
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::YankPop));
            }
            return render();
          }
          if (active_is_action(TuiAction::PageUp))
          {
            auto const [_, height] = terminal_size();
            scroll_up(std::max<std::size_t>(1, height / 2));
            return render();
          }
          if (active_is_action(TuiAction::PageDown))
          {
            auto const [_, height] = terminal_size();
            scroll_down(std::max<std::size_t>(1, height / 2));
            return render();
          }
          if (active_is_action(TuiAction::MessagePrev))
          {
            scroll_to_message_boundary(true);
            return render();
          }
          if (active_is_action(TuiAction::MessageNext))
          {
            scroll_to_message_boundary(false);
            return render();
          }
          if (active_is_action(TuiAction::JumpToBottom))
          {
            jump_to_bottom("live tail");
            return render();
          }
          if (active_is_action(TuiAction::PalettePrev) && slash_palette_active())
          {
            pending_escape_clear = false;
            auto const matches = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands);
            if (matches.empty())
            {
              snapshot.status = "no matching slash commands";
            }
            else
            {
              selected_slash_command_index = previous_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
              snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
            }
            return render();
          }
          if (active_is_action(TuiAction::PalettePrev) && file_reference_palette_active())
          {
            pending_escape_clear = false;
            auto const matches = filter_file_references(draft.text, draft.cursor, snapshot.file_references);
            if (matches.empty())
            {
              snapshot.status = "no matching file references";
            }
            else
            {
              selected_slash_command_index =
                  previous_file_reference_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
              snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
            }
            return render();
          }
          if (active_is_action(TuiAction::PalettePrev) && path_completion_palette_active())
          {
            pending_escape_clear = false;
            auto const matches = filter_path_completions(draft.text, draft.cursor, snapshot.file_references);
            if (matches.empty())
            {
              snapshot.status = "no matching paths";
            }
            else
            {
              selected_slash_command_index =
                  previous_path_completion_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
              snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
            }
            return render();
          }
          if (active_is_action(TuiAction::HistoryPrev))
          {
            pending_escape_clear = false;
            clear_draft_selection();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            if (browse_composer_input_history(draft, input_history, history_index, draft_input, true))
            {
              snapshot.status = "history previous";
            }
            else
            {
              scroll_up(kKeyboardScrollRows);
            }
            return render();
          }
          if (active_event.key == Key::ArrowUp)
          {
            scroll_up(kKeyboardScrollRows);
            return render();
          }
          if (active_is_action(TuiAction::CursorUp) && apply_composer_draft_action(draft, TuiAction::CursorUp))
          {
            pending_escape_clear = false;
            clear_draft_selection();
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            return render();
          }
          if (active_is_action(TuiAction::PaletteNext) && slash_palette_active())
          {
            pending_escape_clear = false;
            auto const matches = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands);
            if (matches.empty())
            {
              snapshot.status = "no matching slash commands";
            }
            else
            {
              selected_slash_command_index = next_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
              snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
            }
            return render();
          }
          if (active_is_action(TuiAction::PaletteNext) && file_reference_palette_active())
          {
            pending_escape_clear = false;
            auto const matches = filter_file_references(draft.text, draft.cursor, snapshot.file_references);
            if (matches.empty())
            {
              snapshot.status = "no matching file references";
            }
            else
            {
              selected_slash_command_index = next_file_reference_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
              snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
            }
            return render();
          }
          if (active_is_action(TuiAction::PaletteNext) && path_completion_palette_active())
          {
            pending_escape_clear = false;
            auto const matches = filter_path_completions(draft.text, draft.cursor, snapshot.file_references);
            if (matches.empty())
            {
              snapshot.status = "no matching paths";
            }
            else
            {
              selected_slash_command_index = next_path_completion_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
              snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
            }
            return render();
          }
          if (active_is_action(TuiAction::HistoryNext))
          {
            pending_escape_clear = false;
            clear_draft_selection();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            path_completion_force_active = false;
            draft_scroll_offset = 0;
            if (browse_composer_input_history(draft, input_history, history_index, draft_input, false))
            {
              snapshot.status = history_index ? "history next" : "history draft";
            }
            else
            {
              scroll_down(kKeyboardScrollRows);
            }
            return render();
          }
          if (active_event.key == Key::ArrowDown)
          {
            scroll_down(kKeyboardScrollRows);
            return render();
          }
          if (active_is_action(TuiAction::CursorDown) && apply_composer_draft_action(draft, TuiAction::CursorDown))
          {
            pending_escape_clear = false;
            clear_draft_selection();
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            return render();
          }
          if (active_event.key == Key::MouseWheelUp)
          {
            scroll_up(kMouseWheelScrollRows);
            return render();
          }
          if (active_event.key == Key::MouseWheelDown)
          {
            scroll_down(kMouseWheelScrollRows);
            return render();
          }
          if (active_is_action(TuiAction::DetailsToggle))
          {
            snapshot.tool_details_visible = !snapshot.tool_details_visible;
            snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
            return render();
          }
          if (active_is_action(TuiAction::VariantCycle))
          {
            snapshot.status = "reasoning can be changed between turns";
            return render();
          }
          if (active_is_action(TuiAction::ThinkingToggle))
          {
            toggle_thinking_visibility();
            return render();
          }
          if (active_is_action(TuiAction::ModelSelect))
          {
            snapshot.status = "model selection is available between turns";
            return render();
          }
          if (active_is_action(TuiAction::ModelCycleForward) || active_is_action(TuiAction::ModelCycleBackward))
          {
            snapshot.status = "model cycling is available between turns";
            return render();
          }
          if (active_is_action(TuiAction::MessageDequeue))
          {
            snapshot.status = "queued-message restore is available during active runs";
            return render();
          }
          if (active_event.key == Key::Space)
          {
            insert_active_text();
            return render();
          }
          return true;
        };
        bool render_failed = false;
        while (submit_future.wait_for(kSpinnerFrameDelay) != std::future_status::ready)
        {
          if (terminal_signal_received())
          {
            if (terminal_signal_number() == SIGINT && !draft.text.empty())
            {
              clear_terminal_signal();
              static_cast<void>(clear_draft_for_interrupt());
              snapshot.selected_slash_command_index = selected_slash_command_index;
              if (!render())
              {
                terminal_write_failed = true;
                render_failed = true;
                fail_pending_prompt_requests();
                break;
              }
              continue;
            }
            request_close_after_submit();
            break;
          }
          if (service_pending_prompt_request(cancel_requested, request_stop))
          {
            auto const drain_result = drain_runtime_events();
            if (drain_result == RuntimeEventDrainResult::RenderFailed)
            {
              terminal_write_failed = true;
              render_failed = true;
              fail_pending_prompt_requests();
              break;
            }
            continue;
          }
          if (auto active_input = poll_curses_input())
          {
            if (!handle_active_input(*active_input))
            {
              terminal_write_failed = true;
              render_failed = true;
              fail_pending_prompt_requests();
              break;
            }
            if (close_after_submit)
              break;
          }
          static_cast<void>(service_pending_prompt_request(cancel_requested, request_stop));
          auto const drain_result = drain_runtime_events();
          if (drain_result == RuntimeEventDrainResult::RenderFailed)
          {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
          if (drain_result == RuntimeEventDrainResult::Rendered)
            continue;
          if (drain_result == RuntimeEventDrainResult::UpdatedNoRender || transcript_scroll_offset > 0)
            continue;
          if (!maybe_reload_display_settings())
          {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
          if (!render_processing_frame())
          {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
        }
        result = submit_future.get();
        if (active_queues && active_queues->finish)
        {
          if (auto finished = active_queues->finish(run_cancel_requested.load()); !finished)
          {
            {
              std::lock_guard<std::recursive_mutex> lock(ui_mutex);
              snapshot.status = finished.error().format();
            }
            render_failed = true;
          }
        }
        if (drain_runtime_events() == RuntimeEventDrainResult::RenderFailed)
        {
          terminal_write_failed = true;
          render_failed = true;
        }
        {
          std::lock_guard<std::mutex> lock(prompt_audit_mutex);
          prompt_audit_sink = nullptr;
        }
        if (render_failed)
          return InputLoopAction::BreakLoop;
      }
      {
        std::lock_guard<std::mutex> lock(prompt_audit_mutex);
        prompt_audit_sink = nullptr;
      }
      if (terminal_signal_received())
        return InputLoopAction::BreakLoop;
      auto const events_received = event_queue.received_any();
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        settle_turn_activity();
      }
      if (!events_received)
      {
        auto const turn_elapsed = std::chrono::steady_clock::now() - turn_started_at;
        auto const assistant_meta = assistant_meta_for_snapshot(snapshot, turn_elapsed);
        if (!is_command_submission)
        {
          push_transcript(snapshot, TranscriptItem{.label = "you", .text = submitted});
        }
        if (run_cancel_requested.load())
        {
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = "stopped by user", .meta = assistant_meta});
        }
        else
        {
          for (auto const& tool : result.tool_timeline)
          {
            push_transcript(snapshot, TranscriptItem{.tool = tool});
          }
          if (!should_show_slash_command_output_as_status(submitted))
          {
            for (auto const& output : result.output)
            {
              push_transcript(snapshot, TranscriptItem{.label = "ava", .text = output, .meta = assistant_meta});
            }
          }
        }
      }
      if (!events_received)
        transcript_scroll_offset = 0;
      auto const show_command_status =
          !events_received && !run_cancel_requested.load() && should_show_slash_command_output_as_status(submitted) && !result.output.empty();
      snapshot.status = show_command_status ? result.output.back()
                                            : (events_received ? (event_state.run_status == TuiEventRunStatus::Error      ? "error"
                                                                  : event_state.run_status == TuiEventRunStatus::Canceled ? "stopped"
                                                                                                                          : "done")
                                                               : (result.output.empty() ? "ok" : "done"));
      snapshot.processing = false;
      if (result.context_source_count)
      {
        snapshot.context_source_count = result.context_source_count;
        sidebar.context_source_count = result.context_source_count;
      }
      refresh_token_status();
      if (!render())
      {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      if (close_after_submit)
        return InputLoopAction::BreakLoop;
      if (result.quit)
        return InputLoopAction::BreakLoop;
      return InputLoopAction::ContinueLoop;
    }
    return InputLoopAction::None;
  };

  if (terminal_signal_received())
    return 130;
  if (!render())
    return 1;

  while (true)
  {
    auto const maybe_input = read_curses_input_with_timeout(kIdleInputPollDelay);
    if (!maybe_input)
    {
      if (!maybe_reload_display_settings())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    auto const input = *maybe_input;
    if (terminal_signal_received())
    {
      if (terminal_signal_number() == SIGINT && !draft.text.empty())
      {
        clear_terminal_signal();
        static_cast<void>(clear_draft_for_interrupt());
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      break;
    }
    if (input.resize)
    {
      if (!render())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    if (snapshot.select_list)
    {
      auto input_result = [&]() {
        if (input.event.key == Key::MouseLeftClick)
        {
          if (auto const clicked = select_list_selection_for_screen_position(snapshot, input.event.mouse_row, input.event.mouse_column))
          {
            auto action = SelectListInputAction::Resolve;
            if (*clicked >= snapshot.select_list->items.size() || !snapshot.select_list->items[*clicked].enabled)
            {
              action = SelectListInputAction::Redraw;
            }
            return SelectListInputResult{.selected_item_index = *clicked, .query = snapshot.select_list->query, .action = action};
          }
        }
        if (active_select_list == ActiveSelectList::ScopedModels)
        {
          auto const scoped_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, input.event.key); };
          if (scoped_action(TuiAction::ModelsSave))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsSave};
          }
          if (scoped_action(TuiAction::ModelsEnableAll))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsEnableAll};
          }
          if (scoped_action(TuiAction::ModelsClearAll))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsClearAll};
          }
          if (scoped_action(TuiAction::ModelsToggleProvider))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsToggleProvider};
          }
          if (scoped_action(TuiAction::ModelsReorderUp))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsReorderUp};
          }
          if (scoped_action(TuiAction::ModelsReorderDown))
          {
            return SelectListInputResult{.selected_item_index = snapshot.select_list->selected_item_index,
                                         .query = snapshot.select_list->query,
                                         .action = SelectListInputAction::ModelsReorderDown};
          }
        }
        return handle_select_list_input(*snapshot.select_list, input.event, options.key_bindings);
      }();
      auto preserve_session_selector_state = [&](SelectListView next_view, std::string status) {
        session_archive_confirmation.reset();
        auto const query = input_result.query;
        std::string selected_value;
        if (input_result.selected_item_index < snapshot.select_list->items.size())
        {
          selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
        }
        next_view.query = query;
        if (!selected_value.empty())
        {
          for (std::size_t index = 0; index < next_view.items.size(); ++index)
          {
            if (next_view.items[index].value == selected_value)
            {
              next_view.selected_item_index = index;
              break;
            }
          }
        }
        next_view.selected_item_index = clamp_select_list_selection(next_view, next_view.selected_item_index);
        snapshot.select_list = std::move(next_view);
        snapshot.status = std::move(status);
      };
      auto selected_select_list_item = [&]() -> SelectListItemView const* {
        if (!snapshot.select_list || input_result.selected_item_index >= snapshot.select_list->items.size())
          return nullptr;
        return &snapshot.select_list->items[input_result.selected_item_index];
      };
      auto visible_select_list_values = [&]() {
        std::vector<std::string> values;
        if (!snapshot.select_list)
          return values;
        auto current = *snapshot.select_list;
        current.query = input_result.query;
        current.selected_item_index = input_result.selected_item_index;
        for (auto const index : filter_select_list_items(current))
        {
          if (index < current.items.size() && current.items[index].enabled && !current.items[index].value.empty())
          {
            values.push_back(current.items[index].value);
          }
        }
        return values;
      };
      auto apply_opened_session_snapshot = [&](TuiRuntimeStateSnapshot state, bool announce) {
        auto status = state.status;
        snapshot.transcript.clear();
        clear_draft_selection();
        reset_composer_draft(draft);
        jump_mode = ComposerJumpMode::None;
        draft_input.clear();
        history_index.reset();
        apply_runtime_state_snapshot(std::move(state));
        if (announce && !status.empty())
        {
          push_transcript(snapshot, TranscriptItem{.label = "ava", .text = std::move(status), .meta = assistant_meta_for_snapshot(snapshot)});
        }
        transcript_scroll_offset = 0;
        draft_scroll_offset = 0;
      };
      if (input_result.action == SelectListInputAction::Redraw && snapshot.select_list)
      {
        session_archive_confirmation.reset();
        snapshot.select_list->selected_item_index = input_result.selected_item_index;
        snapshot.select_list->query = std::move(input_result.query);
      }
      else if (input_result.action == SelectListInputAction::Resolve && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || !selected_item->enabled || selected_item->value.empty())
        {
          snapshot.status = "scoped model cannot be toggled from this row";
          static_cast<void>(beep());
        }
        else if (!options.on_scoped_model_toggled)
        {
          snapshot.status = "scoped model toggle unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_toggled(*snapshot.select_list, selected_item->value);
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped model toggled");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsEnableAll && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        if (!options.on_scoped_model_enable_all)
        {
          snapshot.status = "scoped model enable-all unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_enable_all(*snapshot.select_list, visible_select_list_values());
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped models enabled");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsClearAll && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        if (!options.on_scoped_model_clear_all)
        {
          snapshot.status = "scoped model clear-all unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_clear_all(*snapshot.select_list, visible_select_list_values());
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped models cleared");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsToggleProvider && active_select_list == ActiveSelectList::ScopedModels &&
               snapshot.select_list)
      {
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || selected_item->value.empty())
        {
          snapshot.status = "provider toggle unavailable from this row";
          static_cast<void>(beep());
        }
        else if (!options.on_scoped_model_toggle_provider)
        {
          snapshot.status = "provider toggle unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated = options.on_scoped_model_toggle_provider(*snapshot.select_list, selected_item->value);
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped provider toggled");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if ((input_result.action == SelectListInputAction::ModelsReorderUp || input_result.action == SelectListInputAction::ModelsReorderDown) &&
               active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || selected_item->value.empty())
        {
          snapshot.status = "scoped model reorder unavailable from this row";
          static_cast<void>(beep());
        }
        else if (!options.on_scoped_model_reorder)
        {
          snapshot.status = "scoped model reorder unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto updated =
              options.on_scoped_model_reorder(*snapshot.select_list, selected_item->value, input_result.action == SelectListInputAction::ModelsReorderUp);
          if (updated)
          {
            preserve_session_selector_state(std::move(*updated), "scoped model order updated");
          }
          else
          {
            snapshot.status = updated.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::ModelsSave && active_select_list == ActiveSelectList::ScopedModels && snapshot.select_list)
      {
        if (!options.on_scoped_model_save)
        {
          snapshot.status = "scoped model save unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto saved = options.on_scoped_model_save();
          if (saved)
          {
            snapshot.status = *saved;
          }
          else
          {
            snapshot.status = saved.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::CycleSort && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_sort_cycle)
      {
        preserve_session_selector_state(options.on_session_selector_sort_cycle(), "session selector sort cycled");
      }
      else if (input_result.action == SelectListInputAction::ToggleNamedFilter && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_named_filter_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_named_filter_toggle(), "session selector filter toggled");
      }
      else if (input_result.action == SelectListInputAction::TogglePathDisplay && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_path_display_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_path_display_toggle(), "session selector path display toggled");
      }
      else if (input_result.action == SelectListInputAction::ToggleArchivedFilter && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_archived_filter_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_archived_filter_toggle(), "session selector archived filter toggled");
      }
      else if (input_result.action == SelectListInputAction::ToggleLabelTimestamp && active_select_list == ActiveSelectList::Session && snapshot.select_list &&
               options.on_session_selector_label_timestamp_toggle)
      {
        preserve_session_selector_state(options.on_session_selector_label_timestamp_toggle(), "session selector label timestamps toggled");
      }
      else if (input_result.action == SelectListInputAction::Rename && active_select_list == ActiveSelectList::Session && snapshot.select_list)
      {
        if (input_result.selected_item_index < snapshot.select_list->items.size() && snapshot.select_list->items[input_result.selected_item_index].enabled &&
            !snapshot.select_list->items[input_result.selected_item_index].value.empty())
        {
          auto const selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
          snapshot.select_list.reset();
          active_select_list = ActiveSelectList::None;
          auto draft_text = "/sessions rename " + selected_value + " ";
          clear_draft_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(draft_text)));
          draft_input.clear();
          history_index.reset();
          draft_scroll_offset = 0;
          snapshot.status = "session rename draft ready";
        }
        else
        {
          snapshot.status = "session cannot be renamed from this row";
          static_cast<void>(beep());
        }
      }
      else if (input_result.action == SelectListInputAction::Label && active_select_list == ActiveSelectList::Session && snapshot.select_list)
      {
        if (input_result.selected_item_index < snapshot.select_list->items.size() && snapshot.select_list->items[input_result.selected_item_index].enabled &&
            !snapshot.select_list->items[input_result.selected_item_index].value.empty())
        {
          auto const selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
          snapshot.select_list.reset();
          active_select_list = ActiveSelectList::None;
          auto draft_text = "/sessions labels " + selected_value + " ";
          clear_draft_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(draft_text)));
          draft_input.clear();
          history_index.reset();
          draft_scroll_offset = 0;
          snapshot.status = "session labels draft ready";
        }
        else
        {
          snapshot.status = "session cannot be labeled from this row";
          static_cast<void>(beep());
        }
      }
      else if ((input_result.action == SelectListInputAction::BranchParent || input_result.action == SelectListInputAction::BranchChild) &&
               active_select_list == ActiveSelectList::Session && snapshot.select_list)
      {
        session_archive_confirmation.reset();
        auto const* selected_item = selected_select_list_item();
        if (!selected_item || !selected_item->enabled || selected_item->value.empty())
        {
          snapshot.status = "session branch navigation unavailable from this row";
          static_cast<void>(beep());
        }
        else if (input_result.action == SelectListInputAction::BranchParent && !options.on_session_selector_branch_parent)
        {
          snapshot.status = "session parent navigation unavailable";
          static_cast<void>(beep());
        }
        else if (input_result.action == SelectListInputAction::BranchChild && !options.on_session_selector_branch_child)
        {
          snapshot.status = "session child navigation unavailable";
          static_cast<void>(beep());
        }
        else
        {
          auto opened = input_result.action == SelectListInputAction::BranchParent ? options.on_session_selector_branch_parent(selected_item->value)
                                                                                   : options.on_session_selector_branch_child(selected_item->value);
          if (opened)
          {
            snapshot.select_list.reset();
            active_select_list = ActiveSelectList::None;
            apply_opened_session_snapshot(std::move(*opened), true);
          }
          else
          {
            snapshot.status = opened.error().format();
            static_cast<void>(beep());
          }
        }
      }
      else if (input_result.action == SelectListInputAction::Archive || input_result.action == SelectListInputAction::ArchiveNoninvasive)
      {
        bool const noninvasive_archive = input_result.action == SelectListInputAction::ArchiveNoninvasive;
        auto const* selected_item = selected_select_list_item();
        if (active_select_list != ActiveSelectList::Session || !snapshot.select_list)
        {
          snapshot.select_list.reset();
          active_select_list = ActiveSelectList::None;
          session_archive_confirmation.reset();
          snapshot.status = "view canceled";
        }
        else if (!selected_item || !selected_item->enabled || selected_item->value.empty())
        {
          session_archive_confirmation.reset();
          snapshot.status = "session cannot be archived or restored from this row";
          static_cast<void>(beep());
        }
        else
        {
          bool const archive = selected_item->badge != "archived";
          if (archive && selected_item->current)
          {
            session_archive_confirmation.reset();
            snapshot.status = "switch sessions before archiving the active session";
            static_cast<void>(beep());
          }
          else if (archive && !options.on_session_selector_archive)
          {
            session_archive_confirmation.reset();
            snapshot.status = "session archive unavailable";
            static_cast<void>(beep());
          }
          else if (!archive && !options.on_session_selector_unarchive)
          {
            session_archive_confirmation.reset();
            snapshot.status = "session restore unavailable";
            static_cast<void>(beep());
          }
          else if (session_archive_confirmation && session_archive_confirmation->session_id == selected_item->value &&
                   session_archive_confirmation->archive == archive)
          {
            auto updated = archive ? options.on_session_selector_archive(selected_item->value) : options.on_session_selector_unarchive(selected_item->value);
            session_archive_confirmation.reset();
            if (updated)
            {
              preserve_session_selector_state(std::move(*updated), archive ? "session archived" : "session restored");
            }
            else
            {
              snapshot.status = updated.error().format();
              static_cast<void>(beep());
            }
          }
          else
          {
            session_archive_confirmation = PendingSessionArchiveAction{.session_id = selected_item->value, .archive = archive};
            snapshot.status = std::string("press ") + (noninvasive_archive ? "Ctrl+Backspace" : "Ctrl+D") + " again to " + (archive ? "archive " : "restore ") +
                              selected_item->value;
          }
        }
      }
      else if (input_result.action == SelectListInputAction::Resolve || input_result.action == SelectListInputAction::Cancel)
      {
        std::string selected_value;
        if (input_result.action == SelectListInputAction::Resolve && snapshot.select_list &&
            input_result.selected_item_index < snapshot.select_list->items.size())
        {
          selected_value = snapshot.select_list->items[input_result.selected_item_index].value;
        }
        auto const resolved_list = active_select_list;
        snapshot.select_list.reset();
        active_select_list = ActiveSelectList::None;
        session_archive_confirmation.reset();
        if (input_result.action == SelectListInputAction::Cancel)
        {
          snapshot.status = "view canceled";
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsOpenModels)
        {
          if (!open_model_selector())
          {
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
          }
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsOpenScopedModels)
        {
          if (!open_scoped_model_selector())
          {
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
          }
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsOpenKeybindings)
        {
          snapshot.select_list = hotkeys_select_list_view(options.key_bindings);
          active_select_list = ActiveSelectList::Hotkeys;
          snapshot.status = "keybindings opened";
          transcript_scroll_offset = 0;
        }
        else if (resolved_list == ActiveSelectList::Hotkeys && !selected_value.empty())
        {
          auto draft_command = std::string("/keybindings set ") + selected_value + " ";
          clear_draft_selection();
          static_cast<void>(replace_composer_draft(draft, std::move(draft_command)));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          draft_scroll_offset = 0;
          history_index.reset();
          draft_input.clear();
          snapshot.status = "keybinding edit command drafted";
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsEditKeybindings)
        {
          clear_draft_selection();
          static_cast<void>(replace_composer_draft(draft, "/keybindings set "));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          draft_scroll_offset = 0;
          history_index.reset();
          draft_input.clear();
          snapshot.status = "keybinding edit command drafted";
        }
        else if (resolved_list == ActiveSelectList::Settings && selected_value == kSettingsReloadKeybindings)
        {
          if (!options.on_reload_key_bindings)
          {
            snapshot.status = "reload unavailable";
            push_transcript(snapshot, TranscriptItem{.label = "ava", .text = snapshot.status, .meta = assistant_meta_for_snapshot(snapshot)});
            transcript_scroll_offset = 0;
            static_cast<void>(beep());
          }
          else
          {
            auto reloaded = options.on_reload_key_bindings();
            if (!reloaded)
            {
              snapshot.status = reloaded.error().format();
              push_transcript(snapshot, TranscriptItem{.label = "error", .text = snapshot.status});
              transcript_scroll_offset = 0;
              static_cast<void>(beep());
            }
            else
            {
              options.key_bindings = std::move(reloaded->key_bindings);
              apply_runtime_state_snapshot(std::move(reloaded->state));
              push_transcript(snapshot, TranscriptItem{.label = "ava", .text = "keybindings reloaded", .meta = assistant_meta_for_snapshot(snapshot)});
              transcript_scroll_offset = 0;
            }
          }
        }
        else if (resolved_list == ActiveSelectList::Model && options.on_model_selected)
        {
          auto selected = options.on_model_selected(selected_value);
          if (selected)
          {
            apply_runtime_state_snapshot(std::move(*selected));
          }
          else
          {
            snapshot.status = selected.error().format();
            static_cast<void>(beep());
          }
        }
        else if (resolved_list == ActiveSelectList::Session && options.on_session_selected)
        {
          auto selected = options.on_session_selected(selected_value);
          if (selected)
          {
            apply_opened_session_snapshot(std::move(*selected), false);
          }
          else
          {
            snapshot.status = selected.error().format();
            static_cast<void>(beep());
          }
        }
        else if (resolved_list == ActiveSelectList::Settings && options.on_settings_selected)
        {
          auto selected = options.on_settings_selected(selected_value);
          if (selected)
          {
            auto status = selected->status;
            apply_runtime_state_snapshot(std::move(*selected));
            if (!status.empty())
            {
              push_transcript(snapshot, TranscriptItem{.label = "ava", .text = std::move(status), .meta = assistant_meta_for_snapshot(snapshot)});
              transcript_scroll_offset = 0;
            }
          }
          else
          {
            snapshot.status = selected.error().format();
            static_cast<void>(beep());
          }
        }
        else
        {
          snapshot.status = "view closed";
        }
      }
      if (!render())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    auto const event = input.event;
    auto is_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, event.key); };
    auto select_slash_command = [&]() {
      selected_slash_command_index = clamp_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
      if (auto const disabled_reason = slash_command_selection_disabled_reason(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index))
      {
        snapshot.status = "command disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return;
      }
      clear_draft_selection();
      auto selection = slash_command_selection_text(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "command selected - press Enter to run";
    };
    auto select_file_reference = [&]() {
      selected_slash_command_index = clamp_file_reference_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
      auto selection = file_reference_selection_text(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
      clear_draft_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "file reference selected";
    };
    auto select_path_completion = [&]() {
      selected_slash_command_index =
          clamp_path_completion_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index, path_completion_force_active);
      auto selection =
          path_completion_selection_text(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index, path_completion_force_active);
      clear_draft_selection();
      static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
      selected_slash_command_index = 0;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "path selected";
    };
    auto force_path_completion = [&]() {
      auto const matches = filter_path_completions(draft.text, draft.cursor, snapshot.file_references, true);
      if (matches.empty())
        return false;
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      slash_palette_suppressed = false;
      selected_slash_command_index = 0;
      if (matches.size() == 1)
      {
        auto selection = path_completion_selection_text(draft.text, draft.cursor, snapshot.file_references, 0, true);
        clear_draft_selection();
        static_cast<void>(replace_composer_draft(draft, std::move(selection.text), selection.cursor));
        path_completion_force_active = false;
        draft_scroll_offset = 0;
        snapshot.status = "path selected";
        return true;
      }
      path_completion_force_active = true;
      draft_scroll_offset = 0;
      snapshot.status = "path suggestions";
      return true;
    };
    if (jump_mode != ComposerJumpMode::None)
    {
      if (is_action(TuiAction::JumpForward) || is_action(TuiAction::JumpBackward))
      {
        jump_mode = ComposerJumpMode::None;
        snapshot.status = "jump cancelled";
      }
      else if (auto const target = printable_jump_target(input))
      {
        bool const forward = jump_mode == ComposerJumpMode::Forward;
        jump_mode = ComposerJumpMode::None;
        pending_escape_clear = false;
        history_index.reset();
        draft_input.clear();
        selected_slash_command_index = 0;
        slash_palette_suppressed = false;
        path_completion_force_active = false;
        draft_scroll_offset = 0;
        clear_draft_selection();
        snapshot.status =
            jump_composer_draft_to_character(draft, *target, forward) ? (forward ? "jumped forward" : "jumped backward") : "jump character not found";
      }
      else
      {
        jump_mode = ComposerJumpMode::None;
      }
      if (event.key == Key::Character || event.key == Key::Space || is_action(TuiAction::JumpForward) || is_action(TuiAction::JumpBackward))
      {
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
    }
    bool const ctrl_d_delete_forward = event.key == Key::CtrlD && is_action(TuiAction::DeleteForward) && !draft.text.empty();
    bool const delete_forward_action = is_action(TuiAction::DeleteForward) && (event.key != Key::CtrlD || ctrl_d_delete_forward);

    auto insert_input_text = [&]() {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      auto const text = input.text.empty() ? std::string(1, event.character) : input.text;
      if (input.bracketed_paste)
      {
        static_cast<void>(delete_draft_selection());
        if (insert_composer_paste_text(draft, text))
          snapshot.status = "pasted into draft safely";
      }
      else if (!replace_draft_selection(text))
      {
        static_cast<void>(insert_composer_draft_text(draft, text));
      }
    };
    if (event.key == Key::Character)
    {
      insert_input_text();
    }
    else if (is_action(TuiAction::MessageFollowUp))
    {
      auto const action = handle_submit();
      if (action == InputLoopAction::BreakLoop)
        break;
      if (action == InputLoopAction::ContinueLoop)
        continue;
    }
    else if (is_action(TuiAction::NewLine))
    {
      insert_newline();
    }
    else if (is_action(TuiAction::ExternalEditor))
    {
      if (!open_external_editor())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    else if (is_action(TuiAction::Suspend))
    {
      if (!suspend_to_background())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    else if (is_action(TuiAction::ClipboardPasteImage))
    {
      if (!paste_clipboard_image())
      {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    else if (is_action(TuiAction::JumpForward) || is_action(TuiAction::JumpBackward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      jump_mode = is_action(TuiAction::JumpForward) ? ComposerJumpMode::Forward : ComposerJumpMode::Backward;
      snapshot.status = is_action(TuiAction::JumpForward) ? "jump forward: type character" : "jump backward: type character";
    }
    else if (is_action(TuiAction::DeleteBackward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!delete_draft_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteBackward));
    }
    else if (delete_forward_action)
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!delete_draft_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteForward));
    }
    else if (is_action(TuiAction::DeleteWordBackward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!delete_draft_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordBackward));
    }
    else if (is_action(TuiAction::DeleteWordForward))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!delete_draft_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordForward));
    }
    else if (is_action(TuiAction::DeleteToLineStart))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!delete_draft_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineStart));
    }
    else if (is_action(TuiAction::DeleteToLineEnd))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      if (!delete_draft_selection())
        static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineEnd));
    }
    else if (is_action(TuiAction::CopySelection) && draft_selection_bounds())
    {
      pending_escape_clear = false;
      path_completion_force_active = false;
      static_cast<void>(copy_draft_selection());
    }
    else if (is_action(TuiAction::ClearInput) && (!draft.text.empty() || !is_action(TuiAction::Interrupt)))
    {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      clear_draft_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::ClearInput) ? "input cleared" : "input already empty";
    }
    else if (is_action(TuiAction::AutocompleteAccept) && slash_palette_active())
    {
      pending_escape_clear = false;
      select_slash_command();
    }
    else if (is_action(TuiAction::AutocompleteAccept) && file_reference_palette_active())
    {
      pending_escape_clear = false;
      select_file_reference();
    }
    else if (is_action(TuiAction::AutocompleteAccept) && path_completion_palette_active())
    {
      pending_escape_clear = false;
      select_path_completion();
    }
    else if (is_action(TuiAction::AutocompleteAccept) && force_path_completion())
    {
      pending_escape_clear = false;
    }
    else if (is_action(TuiAction::ModeToggle))
    {
      pending_escape_clear = false;
      path_completion_force_active = false;
      if (!options.on_toggle_mode)
      {
        snapshot.status = "mode toggle unavailable";
      }
      else if (auto result = options.on_toggle_mode(); !result)
      {
        snapshot.status = result.error().format();
      }
      else
      {
        snapshot.mode = *result;
        snapshot.status = "mode switched to " + snapshot.mode;
      }
    }
    else if (is_action(TuiAction::Interrupt))
    {
      path_completion_force_active = false;
      if (!draft.text.empty())
      {
        static_cast<void>(clear_draft_for_interrupt());
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      break;
    }
    else if (is_action(TuiAction::Exit))
    {
      break;
    }
    else if (is_action(TuiAction::VariantCycle))
    {
      pending_escape_clear = false;
      cycle_reasoning();
    }
    else if (is_action(TuiAction::ThinkingToggle))
    {
      pending_escape_clear = false;
      toggle_thinking_visibility();
    }
    else if (is_action(TuiAction::ModelSelect))
    {
      if (!open_model_selector())
      {
        terminal_write_failed = true;
        break;
      }
    }
    else if (is_action(TuiAction::ModelCycleForward))
    {
      pending_escape_clear = false;
      cycle_model(true);
    }
    else if (is_action(TuiAction::ModelCycleBackward))
    {
      pending_escape_clear = false;
      cycle_model(false);
    }
    else if (is_action(TuiAction::SessionResume) || is_action(TuiAction::SessionTree))
    {
      if (!open_session_selector())
      {
        terminal_write_failed = true;
        break;
      }
    }
    else if (is_action(TuiAction::SessionNew) || is_action(TuiAction::SessionFork))
    {
      forced_slash_submission = is_action(TuiAction::SessionNew) ? "/new" : "/fork";
      auto const action = handle_submit();
      if (action == InputLoopAction::BreakLoop)
        break;
      if (action == InputLoopAction::ContinueLoop)
        continue;
    }
    else if (is_action(TuiAction::MessageDequeue))
    {
      pending_escape_clear = false;
      snapshot.status = "queued-message restore is available during active runs";
    }
    else if (is_action(TuiAction::PageUp))
    {
      auto const [_, height] = terminal_size();
      scroll_up(std::max<std::size_t>(1, height / 2));
    }
    else if (is_action(TuiAction::PageDown))
    {
      auto const [_, height] = terminal_size();
      scroll_down(std::max<std::size_t>(1, height / 2));
    }
    else if (is_action(TuiAction::MessagePrev))
    {
      scroll_to_message_boundary(true);
    }
    else if (is_action(TuiAction::MessageNext))
    {
      scroll_to_message_boundary(false);
    }
    else if (is_action(TuiAction::JumpToBottom))
    {
      jump_to_bottom("live tail");
    }
    else if (event.key == Key::MouseWheelUp)
    {
      scroll_up(kMouseWheelScrollRows);
    }
    else if (event.key == Key::MouseWheelDown)
    {
      scroll_down(kMouseWheelScrollRows);
    }
    else if (event.key == Key::MouseLeftClick)
    {
      pending_escape_clear = false;
      if (auto const clicked = slash_palette_selection_for_screen_row(snapshot, event.mouse_row))
      {
        clear_draft_selection();
        selected_slash_command_index = *clicked;
        select_slash_command();
      }
      else if (auto const clicked = file_reference_palette_selection_for_screen_row(snapshot, event.mouse_row))
      {
        clear_draft_selection();
        selected_slash_command_index = *clicked;
        select_file_reference();
      }
      else if (auto const clicked = path_completion_palette_selection_for_screen_row(snapshot, event.mouse_row))
      {
        clear_draft_selection();
        selected_slash_command_index = *clicked;
        select_path_completion();
      }
      else if (auto const cursor = composer_input_cursor_for_screen_position(snapshot, event.mouse_row, event.mouse_column))
      {
        draft.cursor = clamp_composer_draft_cursor_to_atomic_boundary(draft, *cursor);
        draft_selection_anchor = draft.cursor;
        draft_selection_cursor = draft.cursor;
        draft.vertical_column = std::string::npos;
        draft.yank_start = std::string::npos;
        draft.yank_end = std::string::npos;
        history_index.reset();
        draft_input.clear();
        snapshot.status = "cursor moved";
      }
    }
    else if (event.key == Key::MouseLeftDrag || event.key == Key::MouseLeftRelease)
    {
      pending_escape_clear = false;
      if (auto const cursor = composer_input_cursor_for_screen_position(snapshot, event.mouse_row, event.mouse_column))
      {
        auto const next_cursor = clamp_composer_draft_cursor_to_atomic_boundary(draft, *cursor);
        if (draft_selection_anchor == std::string::npos)
          draft_selection_anchor = clamp_composer_draft_cursor_to_atomic_boundary(draft, draft.cursor);
        draft_selection_cursor = next_cursor;
        draft.cursor = next_cursor;
        draft.vertical_column = std::string::npos;
        draft.yank_start = std::string::npos;
        draft.yank_end = std::string::npos;
        history_index.reset();
        draft_input.clear();
        snapshot.status = draft_selection_bounds() ? "selection active" : "cursor moved";
      }
    }
    else if (extend_draft_selection_for_key(event.key))
    {
      // Selection state was updated by the helper.
    }
    else if (event.key == Key::CtrlHome)
    {
      pending_escape_clear = false;
      clear_draft_selection();
      draft.cursor = 0;
      draft.vertical_column = std::string::npos;
      draft.yank_start = std::string::npos;
      draft.yank_end = std::string::npos;
    }
    else if (event.key == Key::CtrlEnd)
    {
      pending_escape_clear = false;
      clear_draft_selection();
      draft.cursor = draft.text.size();
      draft.vertical_column = std::string::npos;
      draft.yank_start = std::string::npos;
      draft.yank_end = std::string::npos;
    }
    else if (is_action(TuiAction::CursorLeft))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLeft));
    }
    else if (is_action(TuiAction::CursorRight))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorRight));
    }
    else if (is_action(TuiAction::CursorLineStart))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineStart));
    }
    else if (is_action(TuiAction::CursorLineEnd))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineEnd));
    }
    else if (is_action(TuiAction::CursorWordLeft))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordLeft));
    }
    else if (is_action(TuiAction::CursorWordRight))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordRight));
    }
    else if (is_action(TuiAction::PalettePrev) && slash_palette_active())
    {
      pending_escape_clear = false;
      auto const matches = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands);
      if (matches.empty())
      {
        snapshot.status = "no matching slash commands";
      }
      else
      {
        selected_slash_command_index = previous_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    }
    else if (is_action(TuiAction::PalettePrev) && file_reference_palette_active())
    {
      pending_escape_clear = false;
      auto const matches = filter_file_references(draft.text, draft.cursor, snapshot.file_references);
      if (matches.empty())
      {
        snapshot.status = "no matching file references";
      }
      else
      {
        selected_slash_command_index = previous_file_reference_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
        snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    }
    else if (is_action(TuiAction::PalettePrev) && path_completion_palette_active())
    {
      pending_escape_clear = false;
      auto const matches = filter_path_completions(draft.text, draft.cursor, snapshot.file_references);
      if (matches.empty())
      {
        snapshot.status = "no matching paths";
      }
      else
      {
        selected_slash_command_index = previous_path_completion_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
        snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    }
    else if (is_action(TuiAction::HistoryPrev))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      if (browse_composer_input_history(draft, input_history, history_index, draft_input, true))
      {
        snapshot.status = "history previous";
      }
      else
      {
        scroll_up(kKeyboardScrollRows);
      }
    }
    else if (event.key == Key::ArrowUp)
    {
      scroll_up(kKeyboardScrollRows);
    }
    else if (is_action(TuiAction::CursorUp) && apply_composer_draft_action(draft, TuiAction::CursorUp))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
    }
    else if (is_action(TuiAction::PaletteNext) && slash_palette_active())
    {
      pending_escape_clear = false;
      auto const matches = filter_slash_commands(draft.text, draft.cursor, snapshot.slash_commands);
      if (matches.empty())
      {
        snapshot.status = "no matching slash commands";
      }
      else
      {
        selected_slash_command_index = next_slash_palette_selection(draft.text, draft.cursor, snapshot.slash_commands, selected_slash_command_index);
        snapshot.status = "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    }
    else if (is_action(TuiAction::PaletteNext) && file_reference_palette_active())
    {
      pending_escape_clear = false;
      auto const matches = filter_file_references(draft.text, draft.cursor, snapshot.file_references);
      if (matches.empty())
      {
        snapshot.status = "no matching file references";
      }
      else
      {
        selected_slash_command_index = next_file_reference_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
        snapshot.status = "reference " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    }
    else if (is_action(TuiAction::PaletteNext) && path_completion_palette_active())
    {
      pending_escape_clear = false;
      auto const matches = filter_path_completions(draft.text, draft.cursor, snapshot.file_references);
      if (matches.empty())
      {
        snapshot.status = "no matching paths";
      }
      else
      {
        selected_slash_command_index = next_path_completion_selection(draft.text, draft.cursor, snapshot.file_references, selected_slash_command_index);
        snapshot.status = "path " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    }
    else if (is_action(TuiAction::HistoryNext))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      path_completion_force_active = false;
      draft_scroll_offset = 0;
      if (browse_composer_input_history(draft, input_history, history_index, draft_input, false))
      {
        snapshot.status = history_index ? "history next" : "history draft";
      }
      else
      {
        scroll_down(kKeyboardScrollRows);
      }
    }
    else if (event.key == Key::ArrowDown)
    {
      scroll_down(kKeyboardScrollRows);
    }
    else if (is_action(TuiAction::CursorDown) && apply_composer_draft_action(draft, TuiAction::CursorDown))
    {
      pending_escape_clear = false;
      clear_draft_selection();
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
    }
    else if (is_action(TuiAction::Undo))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      clear_draft_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Undo) ? "undo" : "nothing to undo";
    }
    else if (is_action(TuiAction::Redo))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      clear_draft_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Redo) ? "redo" : "nothing to redo";
    }
    else if (is_action(TuiAction::Yank))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      clear_draft_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Yank) ? "yanked text" : "nothing to yank";
    }
    else if (is_action(TuiAction::YankPop))
    {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      clear_draft_selection();
      snapshot.status = apply_composer_draft_action(draft, TuiAction::YankPop) ? "yank-pop" : "nothing to yank-pop";
    }
    else if (is_action(TuiAction::DetailsToggle))
    {
      pending_escape_clear = false;
      snapshot.tool_details_visible = !snapshot.tool_details_visible;
      snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
    }
    else if (is_action(TuiAction::PromptAllow) || is_action(TuiAction::PromptDeny))
    {
      pending_escape_clear = false;
      snapshot.status = "prompt action is only available while a prompt is active";
    }
    else if (is_action(TuiAction::Cancel))
    {
      if (draft_selection_bounds())
      {
        clear_draft_selection();
        pending_escape_clear = false;
        snapshot.status.clear();
      }
      else if (slash_palette_active() || file_reference_palette_active() || path_completion_palette_active())
      {
        pending_escape_clear = false;
        slash_palette_suppressed = true;
        selected_slash_command_index = 0;
        path_completion_force_active = false;
        history_index.reset();
        draft_input.clear();
        snapshot.status.clear();
      }
      else if (!draft.text.empty())
      {
        if (pending_escape_clear)
        {
          static_cast<void>(apply_composer_draft_action(draft, TuiAction::ClearInput));
          selected_slash_command_index = 0;
          path_completion_force_active = false;
          history_index.reset();
          draft_input.clear();
          draft_scroll_offset = 0;
          pending_escape_clear = false;
          snapshot.status = "input cleared";
        }
        else
        {
          pending_escape_clear = true;
          snapshot.status = "press Esc again to clear";
        }
      }
      else
      {
        pending_escape_clear = false;
        snapshot.status = "escape ignored";
      }
    }
    else if (is_action(TuiAction::Submit))
    {
      if (event.key == Key::Enter && convert_backslash_enter_to_newline())
      {
        if (!render())
        {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      auto const action = handle_submit();
      if (action == InputLoopAction::BreakLoop)
        break;
      if (action == InputLoopAction::ContinueLoop)
        continue;
    }
    else if (event.key == Key::Space)
    {
      insert_input_text();
    }
    snapshot.selected_slash_command_index = selected_slash_command_index;
    if (!render())
    {
      terminal_write_failed = true;
      break;
    }
  }

  return terminal_signal_received() ? 130 : (terminal_write_failed ? 1 : 0);
}

}  // namespace ava::tui
