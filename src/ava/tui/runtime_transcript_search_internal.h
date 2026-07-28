#pragma once

#include "ava/tui/composer_internal.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::tui {

enum class ActiveSelectList;
struct ComposerSnapshot;
struct InputEvent;
class RuntimeNavigationController;
class RuntimePresentationState;
class RuntimeRenderer;

namespace detail {

inline constexpr std::size_t kMaxTranscriptSearchQueryBytes = 1024;
inline constexpr std::size_t kMaxTranscriptSearchDetailBytes = 240;

struct TranscriptSearchMatch
{
  std::size_t item_index = 0;
  std::string identity;
  std::string detail;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TranscriptSearchProjection
{
  bool available = false;
  std::string identity;
  std::string searchable_text;
  std::string default_detail;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class TranscriptSearchProjectionCache final
{
 public:
  void rebuild_all(ComposerSnapshot const& snapshot, TranscriptLayout const& layout);
  void clear();
  void refresh_after_transcript_mutation(ComposerSnapshot const& snapshot, TranscriptLayout const& layout, std::ptrdiff_t item_index_shift,
                                         std::size_t changed_from_item_index);
  [[nodiscard]] std::vector<TranscriptSearchMatch> matches(std::string_view query) const;
  [[nodiscard]] std::size_t projection_build_count() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  void rebuild_range(ComposerSnapshot const& snapshot, TranscriptLayout const& layout, std::size_t first_item_index, std::size_t past_last_item_index);

  std::vector<TranscriptSearchProjection> projections_;
  std::size_t projection_build_count_ = 0;
};

[[nodiscard]] bool transcript_search_query_valid(std::string_view query) noexcept;
[[nodiscard]] bool transcript_search_literal_match(std::string_view candidate, std::string_view query) noexcept;
[[nodiscard]] std::optional<std::size_t> shift_transcript_search_item_index(std::optional<std::size_t> item_index, std::ptrdiff_t item_index_shift) noexcept;
[[nodiscard]] std::vector<TranscriptSearchMatch> build_transcript_search_matches(ComposerSnapshot const& snapshot, TranscriptLayout const& layout,
                                                                                 std::string_view query);

}  // namespace detail

class TranscriptSearchController final
{
 public:
  TranscriptSearchController(RuntimePresentationState& presentation_state, RuntimeRenderer& renderer, RuntimeNavigationController& navigation,
                             ActiveSelectList& active_select_list);
  TranscriptSearchController(TranscriptSearchController const&) = delete;
  TranscriptSearchController& operator=(TranscriptSearchController const&) = delete;

  [[nodiscard]] bool open(std::string query);
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] std::optional<bool> handle_input(InputEvent const& event);
  void refresh_after_transcript_mutation(std::ptrdiff_t item_index_shift, std::size_t changed_from_item_index);
  void refresh_after_resize();
  void close_before_prompt();

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  void rebuild(std::optional<std::size_t> selected_item_index, std::size_t fallback_selection);
  void restore_saved_viewport(std::string status);
  void refresh_authoritative_layout();

  RuntimePresentationState& presentation_state_;
  RuntimeRenderer& renderer_;
  RuntimeNavigationController& navigation_;
  ActiveSelectList& active_select_list_;
  std::string query_;
  detail::TranscriptSearchProjectionCache projection_cache_;
  std::vector<detail::TranscriptSearchMatch> matches_;
  detail::TranscriptViewportAnchor saved_viewport_anchor_ = {};
  std::size_t saved_scroll_offset_ = 0;
  std::size_t saved_transcript_generation_ = 0;
  std::size_t saved_width_ = 0;
  std::size_t saved_height_ = 0;
  std::ptrdiff_t accumulated_item_index_shift_ = 0;
};

}  // namespace ava::tui
