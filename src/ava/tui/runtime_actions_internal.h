#pragma once

#include "ava/tui/runtime_state_internal.h"

#include <chrono>
#include <optional>
#include <string>
#include "debug.h"

namespace ava::session {
struct ImageAttachmentRef;
}

namespace ava::tui {

struct RuntimeDraftState;
struct TuiRuntimeOptions;
class RuntimeRenderer;

class RuntimeActionController final
{
 public:
  RuntimeActionController(TuiRuntimeOptions& options, RuntimePresentationState& presentation_state, RuntimeDraftState& draft_state, RuntimeRenderer& renderer,
                          ActiveSelectList& active_select_list, std::optional<PendingSessionArchiveAction>& session_archive_confirmation);
  RuntimeActionController(RuntimeActionController const&) = delete;
  RuntimeActionController& operator=(RuntimeActionController const&) = delete;

  [[nodiscard]] bool maybe_reload_display_settings();
  bool clear_draft_for_interrupt();
  [[nodiscard]] bool open_external_editor();
  [[nodiscard]] bool suspend_to_background();
  [[nodiscard]] bool queue_pending_image_attachment(ava::session::ImageAttachmentRef const& imported, std::string label, std::string status,
                                                    std::string transcript_prefix);
  [[nodiscard]] bool paste_clipboard_image();
  void cycle_reasoning();
  void toggle_thinking_visibility();
  [[nodiscard]] bool open_model_selector();
  [[nodiscard]] bool open_scoped_model_selector();
  [[nodiscard]] bool open_session_selector();
  void cycle_model(bool forward);

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  TuiRuntimeOptions& options_;
  RuntimePresentationState& presentation_state_;
  RuntimeDraftState& draft_state_;
  RuntimeRenderer& renderer_;
  ActiveSelectList& active_select_list_;
  std::optional<PendingSessionArchiveAction>& session_archive_confirmation_;
  std::chrono::steady_clock::time_point next_display_reload_check_;
  std::optional<std::string> last_display_reload_error_;
};

}  // namespace ava::tui
