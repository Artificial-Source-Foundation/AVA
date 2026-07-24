#pragma once

#include "ava/tui/runtime_state_internal.h"

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
