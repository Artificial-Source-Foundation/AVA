#pragma once

#include "ava/tui/runtime_state_internal.h"

#include <optional>
#include <string>
#include "debug.h"

namespace ava::tui {

namespace runtime_input {
struct RuntimeInput;
}

struct RuntimeActiveRunState;
struct RuntimeDraftState;
struct TuiRuntimeOptions;
class RuntimeActionController;
class RuntimeNavigationController;
class RuntimePromptCoordinator;
class RuntimeRenderer;
enum class TuiAction;
struct InputEvent;

struct RuntimeActiveRunOutcome
{
  bool break_loop = false;
  bool terminal_write_failed = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class RuntimeActiveRunController final
{
 public:
  RuntimeActiveRunController(TuiRuntimeOptions& options, RuntimePresentationState& presentation_state, RuntimeDraftState& draft_state,
                             RuntimeRenderer& renderer, RuntimePromptCoordinator& prompt_coordinator, RuntimeNavigationController& navigation,
                             RuntimeActionController& action_controller);
  RuntimeActiveRunController(RuntimeActiveRunController const&) = delete;
  RuntimeActiveRunController& operator=(RuntimeActiveRunController const&) = delete;

  [[nodiscard]] RuntimeActiveRunOutcome run(std::string submitted);

 private:
  [[nodiscard]] bool handle_input(RuntimeActiveRunState& state, runtime_input::RuntimeInput const& input);
  [[nodiscard]] RuntimeEventDrainResult drain_events(RuntimeActiveRunState& state);
  [[nodiscard]] bool request_stop(RuntimeActiveRunState& state);
  void request_close_after_submit(RuntimeActiveRunState& state);
  void upsert_stopping_activity();
  void settle_turn_activity(RuntimeActiveRunState& state);

  // Ordered input-handling decomposition. handle_input() dispatches to these
  // focused precedence handlers in the exact order listed below; each returns
  // Unhandled when it does not claim the input so dispatch falls through.
  enum class InputHandling
  {
    Unhandled,
    Handled,
    RenderFailed
  };

  [[nodiscard]] bool is_action(InputEvent const& event, TuiAction action) const;
  [[nodiscard]] static InputHandling to_input_handling(bool render_result);

  [[nodiscard]] ComposerSnapshot const& completion_snapshot();
  [[nodiscard]] bool restore_latest_queued_message(RuntimeActiveRunState& state);
  [[nodiscard]] std::optional<bool> run_active_command(RuntimeActiveRunState& state);
  [[nodiscard]] std::optional<bool> reject_disabled_visible_completion();
  [[nodiscard]] bool queue_active_draft(RuntimeActiveRunState& state, bool follow_up_only);
  void insert_active_text(runtime_input::RuntimeInput const& active_input);

  InputHandling handle_preemptive_input(RuntimeActiveRunState& state, runtime_input::RuntimeInput const& active_input);
  InputHandling handle_completion_acceptance(runtime_input::RuntimeInput const& active_input);
  InputHandling handle_active_command_input(RuntimeActiveRunState& state, runtime_input::RuntimeInput const& active_input);
  InputHandling handle_composer_edit(runtime_input::RuntimeInput const& active_input);
  InputHandling handle_navigation_input(runtime_input::RuntimeInput const& active_input);
  InputHandling handle_navigation_next_input(runtime_input::RuntimeInput const& active_input);
  InputHandling handle_mouse_input(runtime_input::RuntimeInput const& active_input);
  InputHandling handle_restricted_toggle_input(runtime_input::RuntimeInput const& active_input);

  TuiRuntimeOptions& options_;
  RuntimePresentationState& presentation_state_;
  RuntimeDraftState& draft_state_;
  RuntimeRenderer& renderer_;
  RuntimePromptCoordinator& prompt_coordinator_;
  RuntimeNavigationController& navigation_;
  RuntimeActionController& action_controller_;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::tui
