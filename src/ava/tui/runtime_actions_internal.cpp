#include "sys.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/runtime.h"
#include "ava/tui/runtime_actions_internal.h"
#include "ava/tui/runtime_draft_internal.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/terminal.h"
#include "ava/tui/terminal_image.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <curses.h>

namespace ava::tui {
namespace {

constexpr auto kDisplayReloadPollInterval = std::chrono::milliseconds(500);

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
                                        .base64_data = std::make_shared<std::string const>(runtime_transcript::base64_encode(loaded->bytes)),
                                        .dimensions = dimensions,
                                        .image_id = kitty_image_id_from_sha(attachment.sha256)};
}

}  // namespace

RuntimeActionController::RuntimeActionController(TuiRuntimeOptions& options, RuntimePresentationState& presentation_state, RuntimeDraftState& draft_state,
                                                 RuntimeRenderer& renderer, ActiveSelectList& active_select_list,
                                                 std::optional<PendingSessionArchiveAction>& session_archive_confirmation)
    : options_(options),
      presentation_state_(presentation_state),
      draft_state_(draft_state),
      renderer_(renderer),
      active_select_list_(active_select_list),
      session_archive_confirmation_(session_archive_confirmation),
      next_display_reload_check_(std::chrono::steady_clock::now() + kDisplayReloadPollInterval)
{
}

bool RuntimeActionController::maybe_reload_display_settings()
{
  if (!options_.on_maybe_reload_display_settings)
    return true;
  auto const now = std::chrono::steady_clock::now();
  if (now < next_display_reload_check_)
    return true;
  next_display_reload_check_ = now + kDisplayReloadPollInterval;

  auto reloaded = options_.on_maybe_reload_display_settings();
  if (!reloaded)
  {
    auto status = reloaded.error().format();
    if (last_display_reload_error_ && *last_display_reload_error_ == status)
      return true;
    last_display_reload_error_ = status;
    static_cast<void>(beep());
    {
      std::lock_guard<std::recursive_mutex> lock(renderer_.ui_mutex);
      presentation_state_.snapshot.status = std::move(status);
    }
    return renderer_.render();
  }
  last_display_reload_error_.reset();
  if (!*reloaded)
    return true;

  auto state = std::move(**reloaded);
  auto status = state.status.empty() ? std::string("display theme auto-reloaded") : state.status;
  {
    std::lock_guard<std::recursive_mutex> lock(renderer_.ui_mutex);
    presentation_state_.apply_runtime_state_snapshot(options_, std::move(state));
    auto& snapshot = presentation_state_.snapshot;
    if (!snapshot.processing && !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list)
    {
      runtime_transcript::push_transcript(snapshot, TranscriptItem{.label = "ava", .text = status});
      renderer_.transcript_scroll_offset = 0;
    }
    snapshot.status = std::move(status);
  }
  return renderer_.render();
}

bool RuntimeActionController::clear_draft_for_interrupt()
{
  draft_state_.pending_escape_clear = false;
  draft_state_.jump_mode = ComposerJumpMode::None;
  draft_state_.history_index.reset();
  draft_state_.draft_input.clear();
  draft_state_.selected_slash_command_index = 0;
  draft_state_.slash_palette_suppressed = false;
  draft_state_.path_completion_force_active = false;
  draft_state_.draft_scroll_offset = 0;
  draft_state_.clear_selection();
  presentation_state_.snapshot.status.clear();
  return apply_composer_draft_action(draft_state_.draft, TuiAction::ClearInput);
}

bool RuntimeActionController::open_external_editor()
{
  auto& snapshot = presentation_state_.snapshot;
  if (!options_.on_external_editor)
  {
    snapshot.status = "external editor unavailable";
    static_cast<void>(beep());
    return renderer_.render();
  }
  draft_state_.pending_escape_clear = false;
  draft_state_.jump_mode = ComposerJumpMode::None;
  draft_state_.history_index.reset();
  draft_state_.draft_input.clear();
  draft_state_.selected_slash_command_index = 0;
  draft_state_.slash_palette_suppressed = false;
  draft_state_.path_completion_force_active = false;
  draft_state_.draft_scroll_offset = 0;
  draft_state_.clear_selection();
  snapshot.status = "opening external editor";
  if (!renderer_.render())
    return false;

  def_prog_mode();
  endwin();
  auto edited = options_.on_external_editor(draft_state_.draft.text);
  reset_prog_mode();
  clearok(stdscr, TRUE);
  refresh();

  if (!edited)
  {
    snapshot.status = edited.error().format();
    static_cast<void>(beep());
    return renderer_.render();
  }
  if (!*edited)
  {
    snapshot.status = "external editor canceled";
    return renderer_.render();
  }
  snapshot.status =
      replace_composer_draft(draft_state_.draft, std::move(**edited)) ? "external editor updated draft" : "external editor closed without changes";
  return renderer_.render();
}

bool RuntimeActionController::suspend_to_background()
{
  auto& snapshot = presentation_state_.snapshot;
  draft_state_.pending_escape_clear = false;
  draft_state_.jump_mode = ComposerJumpMode::None;
  draft_state_.history_index.reset();
  draft_state_.draft_input.clear();
  draft_state_.selected_slash_command_index = 0;
  draft_state_.slash_palette_suppressed = false;
  draft_state_.path_completion_force_active = false;
  draft_state_.draft_scroll_offset = 0;
  draft_state_.clear_selection();

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
      return renderer_.render();
    }
    reset_prog_mode();
    clearok(stdscr, TRUE);
    refresh();
  }

  snapshot.status = "resumed from background";
  return renderer_.render();
}

bool RuntimeActionController::queue_pending_image_attachment(ava::session::ImageAttachmentRef const& imported, std::string label, std::string status,
                                                             std::string transcript_prefix)
{
  if (label.empty())
    label = imported.id;
  auto const image_capabilities = active_terminal_image_capabilities();
  auto const detail = attachment_detail(imported, image_capabilities);
  auto const preview = attachment_preview(imported, image_capabilities, options_.on_load_image_attachment);
  presentation_state_.pending_image_attachments.push_back(imported);
  auto& snapshot = presentation_state_.snapshot;
  snapshot.pending_attachments.push_back(PendingAttachmentItem{.label = label, .detail = detail, .preview = preview});
  snapshot.status = std::move(status);
  runtime_transcript::push_transcript(
      snapshot, TranscriptItem{.label = "status", .text = transcript_prefix + ": " + label + " " + detail + "\nwill be sent with the next prompt"});
  renderer_.transcript_scroll_offset = 0;
  return renderer_.render();
}

bool RuntimeActionController::paste_clipboard_image()
{
  auto& snapshot = presentation_state_.snapshot;
  draft_state_.pending_escape_clear = false;
  draft_state_.jump_mode = ComposerJumpMode::None;
  draft_state_.history_index.reset();
  draft_state_.draft_input.clear();
  draft_state_.selected_slash_command_index = 0;
  draft_state_.slash_palette_suppressed = false;
  draft_state_.path_completion_force_active = false;
  draft_state_.draft_scroll_offset = 0;
  draft_state_.clear_selection();
  if (!options_.on_paste_clipboard_image)
  {
    snapshot.status = "clipboard image paste unavailable";
    static_cast<void>(beep());
    return renderer_.render();
  }
  snapshot.status = "reading clipboard image";
  if (!renderer_.render())
    return false;

  auto imported = options_.on_paste_clipboard_image();
  if (!imported)
  {
    snapshot.status = imported.error().format();
    static_cast<void>(beep());
    return renderer_.render();
  }
  if (!*imported)
  {
    snapshot.status = "no clipboard image available";
    static_cast<void>(beep());
    return renderer_.render();
  }
  return queue_pending_image_attachment(**imported, "clipboard image", "pasted clipboard image for next prompt", "attached clipboard image");
}

void RuntimeActionController::cycle_reasoning()
{
  auto& snapshot = presentation_state_.snapshot;
  clear_reasoning_feedback_for_user_input(snapshot);
  if (!options_.on_cycle_reasoning)
  {
    snapshot.status = "reasoning cycling unavailable";
    return;
  }
  auto result = options_.on_cycle_reasoning();
  if (!result)
  {
    snapshot.status = result.error().format();
    return;
  }
  apply_reasoning_cycle_success(snapshot, std::move(*result));
  presentation_state_.refresh_reasoning_status(options_);
}

void RuntimeActionController::toggle_thinking_visibility()
{
  auto& snapshot = presentation_state_.snapshot;
  snapshot.thinking_visible = !snapshot.thinking_visible;
  snapshot.status = snapshot.thinking_visible ? "thinking visible" : "thinking hidden";
  renderer_.transcript_scroll_offset = 0;
}

bool RuntimeActionController::open_model_selector()
{
  auto& snapshot = presentation_state_.snapshot;
  if (!options_.model_selector_view)
  {
    snapshot.status = "model selector unavailable";
    static_cast<void>(beep());
    return true;
  }
  draft_state_.pending_escape_clear = false;
  session_archive_confirmation_.reset();
  snapshot.select_list = options_.model_selector_view();
  active_select_list_ = ActiveSelectList::Model;
  snapshot.status = "model selector opened";
  renderer_.transcript_scroll_offset = 0;
  return renderer_.request_render();
}

bool RuntimeActionController::open_scoped_model_selector()
{
  auto& snapshot = presentation_state_.snapshot;
  if (!options_.scoped_model_selector_view)
  {
    snapshot.status = "scoped model selector unavailable";
    static_cast<void>(beep());
    return true;
  }
  draft_state_.pending_escape_clear = false;
  session_archive_confirmation_.reset();
  snapshot.select_list = options_.scoped_model_selector_view();
  active_select_list_ = ActiveSelectList::ScopedModels;
  snapshot.status = "scoped model selector opened";
  renderer_.transcript_scroll_offset = 0;
  return renderer_.request_render();
}

bool RuntimeActionController::open_session_selector()
{
  auto& snapshot = presentation_state_.snapshot;
  if (!options_.session_selector_view)
  {
    snapshot.status = "session selector unavailable";
    static_cast<void>(beep());
    return true;
  }
  draft_state_.pending_escape_clear = false;
  session_archive_confirmation_.reset();
  snapshot.select_list = options_.session_selector_view();
  active_select_list_ = ActiveSelectList::Session;
  snapshot.status = "session selector opened";
  renderer_.transcript_scroll_offset = 0;
  return renderer_.request_render();
}

void RuntimeActionController::cycle_model(bool forward)
{
  auto& snapshot = presentation_state_.snapshot;
  if (!options_.on_cycle_model)
  {
    snapshot.status = "model cycling unavailable";
    static_cast<void>(beep());
    return;
  }
  auto result = options_.on_cycle_model(forward);
  if (!result)
  {
    snapshot.status = result.error().format();
    static_cast<void>(beep());
    return;
  }
  presentation_state_.apply_runtime_state_snapshot(options_, std::move(*result));
  renderer_.transcript_scroll_offset = 0;
}

}  // namespace ava::tui
