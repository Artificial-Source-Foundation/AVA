#pragma once

#include "ava/app/EventEnvelope.h"
#include "ava/app/events.h"
#include "ava/tui/composer.h"
#include "ava/tui/session_grants.h"
#include "ava/session/attachments.h"
#include "ava/core/result.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "debug.h"

namespace ava::tui {

struct TuiRuntimeOptions;
struct TuiRuntimeStateSnapshot;

struct EventEnvelopeQueue
{
  [[nodiscard]] ava::app::EventEnvelopeSink sink();
  [[nodiscard]] std::vector<ava::app::EventEnvelope> drain();
  [[nodiscard]] bool received_any();

 private:
  std::mutex mutex;
  std::vector<ava::app::EventEnvelope> events;
  bool received = false;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
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

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class RuntimePresentationState final
{
 public:
  explicit RuntimePresentationState(TuiRuntimeOptions& options);

  void refresh_token_status(TuiRuntimeOptions const& options);
  void refresh_reasoning_status(TuiRuntimeOptions const& options);
  void apply_runtime_state_snapshot(TuiRuntimeOptions const& options, TuiRuntimeStateSnapshot state);

  ComposerSnapshot snapshot;

 private:
  std::optional<std::size_t> applied_slash_catalog_generation_;
  std::optional<std::size_t> applied_workspace_catalog_generation_;

 public:
  SidebarSnapshot sidebar;
  std::vector<ava::session::ImageAttachmentRef> pending_image_attachments;
  TuiSessionGrantRegistry command_session_grants;

 private:
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::tui
