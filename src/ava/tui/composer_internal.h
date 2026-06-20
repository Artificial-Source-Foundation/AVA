#pragma once

#include "ava/tui/composer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui::detail {

inline constexpr std::size_t kMinWidth = 20;
inline constexpr std::size_t kMinHeight = 8;
inline constexpr std::size_t kMinComposerBlockLines = 4;
inline constexpr std::size_t kMaxComposerBlockLines = 8;
inline constexpr std::size_t kMaxPaletteLines = 8;
inline constexpr std::string_view kReverseVideo = "\x1b[7m";

// The curses draw path currently maps these truecolor SGR forms directly.
// Extend that parser before changing these to 256-color or basic ANSI codes.
inline constexpr std::string_view kSgrReset = "\x1b[0m";
inline constexpr std::string_view kSgrBold = "\x1b[1m";
inline constexpr std::string_view kSgrText = "\x1b[38;2;232;236;241m";
inline constexpr std::string_view kSgrMuted = "\x1b[38;2;139;149;165m";
inline constexpr std::string_view kSgrDim = kSgrMuted;
inline constexpr std::string_view kSgrThinking = "\x1b[38;2;88;96;112m";
inline constexpr std::string_view kSgrTextDimmed = "\x1b[38;2;145;157;178m";
inline constexpr std::string_view kSgrSuccess = "\x1b[38;2;52;211;153m";
inline constexpr std::string_view kSgrWarning = "\x1b[38;2;251;191;36m";
inline constexpr std::string_view kSgrError = "\x1b[38;2;248;113;113m";
inline constexpr std::string_view kSgrAccent = "\x1b[38;2;77;158;246m";
inline constexpr std::string_view kSgrScreenBg = "\x1b[48;2;11;14;20m";
inline constexpr std::string_view kSgrComposerBg = "\x1b[48;2;26;31;46m";
inline constexpr std::string_view kComposerBar = "▎";
inline constexpr std::string_view kComposerPrompt = "❯";

struct ComposerInputLayout {
  std::size_t top_padding = 0;
  std::size_t first_visible = 0;
  std::size_t visible_input_lines = 1;
  std::size_t hidden_above = 0;
  std::size_t hidden_below = 0;
};

[[nodiscard]] bool is_utf8_continuation(unsigned char byte);
[[nodiscard]] std::size_t utf8_sequence_length(unsigned char byte);
[[nodiscard]] bool decode_utf8_codepoint(std::string_view text, std::size_t start, std::size_t length,
                                         char32_t& codepoint);
[[nodiscard]] std::size_t codepoint_columns(char32_t codepoint);
[[nodiscard]] bool skip_sgr_sequence(std::string_view text, std::size_t& index);
[[nodiscard]] std::size_t terminal_text_columns(std::string_view text);
[[nodiscard]] std::string fit_line(std::string text, std::size_t width);
[[nodiscard]] std::string fit_line_preserving_sgr(std::string text, std::size_t width);
[[nodiscard]] std::string surface_line(std::string_view background_sgr, std::string line, std::size_t width);
[[nodiscard]] std::string screen_surface_line(std::string line, std::size_t width);
[[nodiscard]] std::string composer_surface_line(std::string line, std::size_t width);
[[nodiscard]] std::vector<std::string> wrap_transcript_text(std::string_view text, std::size_t width);

[[nodiscard]] std::string slash_command_prefix(std::string_view input);
[[nodiscard]] std::vector<std::string> render_slash_palette(ComposerSnapshot const& snapshot, std::size_t width,
                                                            std::size_t max_lines);
[[nodiscard]] std::vector<std::string> render_file_reference_palette(ComposerSnapshot const& snapshot, std::size_t width,
                                                                     std::size_t max_lines);
[[nodiscard]] std::vector<std::string> render_path_completion_palette(ComposerSnapshot const& snapshot, std::size_t width,
                                                                      std::size_t max_lines);

[[nodiscard]] std::vector<std::string> render_permission_prompt(PermissionPromptView const& prompt, std::size_t width,
                                                                std::size_t max_lines);
[[nodiscard]] std::vector<std::string> render_question_prompt(QuestionPromptView const& prompt, std::size_t width,
                                                              std::size_t max_lines);
[[nodiscard]] std::vector<std::string> render_question_modal(QuestionPromptView const& prompt, std::size_t width,
                                                             std::size_t max_lines);
[[nodiscard]] std::vector<std::string> render_select_list_modal(SelectListView const& view, std::size_t width,
                                                                std::size_t max_lines);
[[nodiscard]] std::vector<std::string> render_unified_diff_body(std::string_view diff, bool diff_truncated,
                                                                std::size_t width, std::string_view line_prefix,
                                                                std::size_t max_lines);

[[nodiscard]] std::string render_generic_line(std::string const& text, std::size_t width);
[[nodiscard]] std::vector<std::string> render_transcript_lines(std::vector<TranscriptItem> const& transcript,
                                                               std::size_t width, bool tool_details_visible = false,
                                                               bool thinking_visible = true);
[[nodiscard]] std::vector<std::size_t> transcript_message_start_lines(std::vector<TranscriptItem> const& transcript,
                                                                      std::size_t width,
                                                                      bool tool_details_visible = false,
                                                                      bool thinking_visible = true);
[[nodiscard]] std::vector<std::string> visible_transcript_lines(std::vector<std::string> const& rendered_transcript,
                                                                std::size_t width, std::size_t transcript_height,
                                                                std::size_t transcript_scroll_offset);

[[nodiscard]] std::vector<std::string> input_render_lines(std::string_view input);
[[nodiscard]] std::size_t composer_block_line_count(ComposerSnapshot const& snapshot, std::size_t height);
[[nodiscard]] ComposerInputLayout composer_input_layout(std::size_t input_line_count, std::size_t max_lines,
                                                        std::size_t draft_scroll_offset);
[[nodiscard]] std::vector<std::string> render_composer_block(ComposerSnapshot const& snapshot, std::size_t width,
                                                             std::size_t max_lines);
[[nodiscard]] std::size_t input_cursor_column(ComposerSnapshot const& snapshot, std::size_t width);
[[nodiscard]] std::size_t input_cursor_line(ComposerSnapshot const& snapshot);

}  // namespace ava::tui::detail
