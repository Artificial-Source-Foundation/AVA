#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/events.h"
#include "ava/agent/question.h"
#include "ava/session/attachments.h"
#include "ava/permissions/permission.h"
#include "ava/core/result.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ava::app::runtime {

// Carry per-invocation run controls for run_prompt and compaction summary generation: credentials, streaming/retry toggles, callbacks for events, permissions,
// questions, cancellation and steering, plus an optional session lock and image attachments.
//
// All callback members default to null; run_prompt treats a null permission or question resolver as an error and a null event sink as a no-op.
struct RuntimeRunOptions
{
  std::string access_token;
  std::string credential_type = "bearer";
  bool openai_oauth = false;
  std::string openai_account_id;
  bool stream = true;
  bool enable_transport_retries = false;
  RuntimeEventSink event_sink = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  ava::agent::QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  std::mutex* session_mutex = nullptr;
  std::vector<ava::session::ImageAttachmentRef> image_attachments;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
