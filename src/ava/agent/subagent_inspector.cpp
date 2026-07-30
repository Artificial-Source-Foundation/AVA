#include "sys.h"
#include "ava/agent/subagent_inspector.h"
#include "ava/session/transcript.h"

#include <utility>

namespace ava::agent {
namespace {

ava::session::TranscriptLimits inspector_transcript_limits() noexcept
{
  return ava::session::TranscriptLimits{
      .max_items = kSubagentInspectorMaxMessages,
      .max_text_bytes = kSubagentInspectorMaxTextBytes,
      .max_item_text_bytes = kSubagentInspectorMaxItemTextBytes,
  };
}

}  // namespace

struct SubagentLiveInspectionSource::Impl
{
  explicit Impl(ava::session::SessionReadAuthority authority_in) : authority(std::move(authority_in)) { }

  ava::session::SessionReadAuthority authority;
};

std::string_view to_string(SubagentLiveMessageRole role) noexcept
{
  switch (role)
  {
    case SubagentLiveMessageRole::User:
      return "user";
    case SubagentLiveMessageRole::Assistant:
      return "assistant";
  }
  return "user";
}

SubagentLiveInspectionSource::SubagentLiveInspectionSource(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) { }

SubagentLiveInspectionSource::~SubagentLiveInspectionSource() = default;

ava::core::Result<std::shared_ptr<SubagentLiveInspectionSource>> SubagentLiveInspectionSource::create(ava::session::SessionReadAuthority authority)
{
  if (!authority.active())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "subagent inspection source requires an active read authority"));
  try
  {
    auto impl = std::make_unique<Impl>(std::move(authority));
    return std::shared_ptr<SubagentLiveInspectionSource>(new SubagentLiveInspectionSource(std::move(impl)));
  }
  catch (...)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "failed to allocate subagent inspection source"));
  }
}

std::string const& SubagentLiveInspectionSource::session_id() const noexcept
{
  return impl_->authority.session_id();
}

bool SubagentLiveInspectionSource::is_ephemeral() const noexcept
{
  return impl_->authority.is_ephemeral();
}

ava::core::Result<ava::session::SessionContentFingerprint> SubagentLiveInspectionSource::content_fingerprint() const
{
  return impl_->authority.content_fingerprint();
}

ava::core::Result<std::vector<ava::session::SessionEntry>> SubagentLiveInspectionSource::load_strict_capped() const
{
  return impl_->authority.load_bounded(subagent_inspector_read_limits());
}

ava::core::Result<std::shared_ptr<SubagentInspectorFrame const>> SubagentLiveInspectionSource::project(std::uint64_t generation, bool terminal,
                                                                                                      bool freeze_pending) const
{
  auto entries = load_strict_capped();
  if (!entries)
    return std::unexpected(std::move(entries.error()));

  // Reuses the existing public committed user/assistant transcript projection.
  // Tools and reasoning remain excluded by that boundary.
  auto transcript = ava::session::project_transcript(*entries, inspector_transcript_limits());
  if (!transcript)
    return std::unexpected(std::move(transcript.error()));

  auto frame = std::make_shared<SubagentInspectorFrame>();
  frame->generation = generation;
  frame->terminal = terminal;
  frame->freeze_pending = freeze_pending;
  frame->unavailable = false;
  frame->not_modified = false;
  frame->truncated = false;
  frame->messages.reserve(transcript->size());
  for (auto const& item : *transcript)
  {
    frame->messages.push_back(SubagentLiveMessage{
        .role = item.role == ava::session::TranscriptRole::User ? SubagentLiveMessageRole::User : SubagentLiveMessageRole::Assistant,
        .text = item.text,
    });
  }
  return std::shared_ptr<SubagentInspectorFrame const>(std::move(frame));
}

std::shared_ptr<SubagentInspectorFrame const> make_unavailable_inspection_frame(std::uint64_t generation, bool terminal, bool freeze_pending)
{
  auto frame = std::make_shared<SubagentInspectorFrame>();
  frame->generation = generation;
  frame->terminal = terminal;
  frame->freeze_pending = freeze_pending;
  frame->unavailable = true;
  return std::shared_ptr<SubagentInspectorFrame const>(std::move(frame));
}

std::shared_ptr<SubagentInspectorFrame const> make_not_modified_inspection_frame(std::uint64_t generation, bool terminal, bool freeze_pending)
{
  auto frame = std::make_shared<SubagentInspectorFrame>();
  frame->generation = generation;
  frame->terminal = terminal;
  frame->freeze_pending = freeze_pending;
  frame->not_modified = true;
  return std::shared_ptr<SubagentInspectorFrame const>(std::move(frame));
}

}  // namespace ava::agent
