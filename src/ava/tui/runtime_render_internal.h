#pragma once

#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_draft_internal.h"

#include <csignal>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>
#include "debug.h"

namespace ava::tui {

class SignalBlockGuard
{
 public:
  SignalBlockGuard();
  SignalBlockGuard(SignalBlockGuard const&) = delete;
  SignalBlockGuard& operator=(SignalBlockGuard const&) = delete;
  ~SignalBlockGuard();

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  sigset_t previous_{};
  bool active_ = false;
};

[[nodiscard]] std::pair<std::size_t, std::size_t> terminal_size();

class RuntimeRenderer final
{
 public:
  RuntimeRenderer(ComposerSnapshot& snapshot, SidebarSnapshot& sidebar, RuntimeDraftState& draft_state);

  [[nodiscard]] bool render();
  [[nodiscard]] bool render_processing_frame();

  std::size_t transcript_scroll_offset = 0;
  std::size_t detached_new_output_count = 0;
  detail::CompletionMatchCache completion_cache;
  detail::TranscriptLayoutCache transcript_layout_cache;
  std::optional<SidebarSnapshot> detached_sidebar_snapshot;
  std::recursive_mutex ui_mutex;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  ComposerSnapshot& snapshot_;
  SidebarSnapshot& sidebar_;
  RuntimeDraftState& draft_state_;
};

}  // namespace ava::tui
