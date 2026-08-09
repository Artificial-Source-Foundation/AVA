#pragma once

#include "ava/tui/mermaid_projection.h"
#include "ava/tui/runtime.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ava::tui {

struct MermaidPresentationServiceResult
{
  bool visual_changed = false;
  std::size_t earliest_changed_item = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// TUI-owned reconciliation state. This class performs no process work itself;
// it invokes only the neutral nonblocking bridge from a runtime update seam.
class RuntimeMermaidPresentationController final
{
 public:
  explicit RuntimeMermaidPresentationController(TuiMermaidRenderBridge bridge);
  RuntimeMermaidPresentationController(RuntimeMermaidPresentationController const&) = delete;
  RuntimeMermaidPresentationController& operator=(RuntimeMermaidPresentationController const&) = delete;
  ~RuntimeMermaidPresentationController();

  [[nodiscard]] MermaidPresentationServiceResult service(ComposerSnapshot& snapshot);
  void shutdown() noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Record
  {
    std::uint64_t item_identity = 0;
    std::uint64_t block_identity = 0;
    std::uint64_t request_identity = 0;
    std::size_t item_index = 0;
    detail::MermaidFenceBlock block;
    bool submitted = false;
    bool pending = false;
    bool failed = false;
    std::optional<std::string> accepted_text = std::nullopt;

    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

  [[nodiscard]] std::uint64_t next_identity() noexcept;
  void cancel_record(Record const& record) noexcept;
  void cancel_all() noexcept;
  void reconcile(ComposerSnapshot const& snapshot);
  void enqueue_waiting() noexcept;
  void drain_completions() noexcept;
  void enforce_result_byte_bound() noexcept;
  [[nodiscard]] std::vector<detail::MermaidAcceptedPresentation> accepted_presentations() const;

  TuiMermaidRenderBridge bridge_;
  std::vector<Record> records_;
  std::uint64_t config_epoch_ = 0;
  bool enabled_ = false;
  std::string session_id_;
  std::size_t observed_transcript_generation_ = 0;
  std::uint64_t next_identity_ = 1;
  bool shutdown_ = false;
};

}  // namespace ava::tui
