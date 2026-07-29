#include "sys.h"
#include "tests/support/test_harness.h"
#include "tests/support/tui_test_support.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime_internal.h"
#include "ava/tui/terminal.h"
#include "ava/tui/terminal_image.h"
#include "ava/tui/text_wrap.h"

#include <algorithm>
#include <clocale>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <curses.h>
#include <unistd.h>

namespace {
void test_tui_terminal_image_support()
{
  auto const unknown = ava::tui::detect_terminal_image_capabilities(ava::tui::TerminalEnvironment{});
  expect(unknown.images == ava::tui::TerminalImageProtocol::None && !unknown.true_color && !unknown.hyperlinks &&
             unknown.detail.find("unknown terminal") != std::string::npos,
         "terminal image detection keeps unknown terminals on the textual fallback path");

  auto const tmux_ghostty = ava::tui::detect_terminal_image_capabilities(
      ava::tui::TerminalEnvironment{.term_program = "ghostty", .term = "tmux-256color", .color_term = "truecolor", .tmux = true}, true);
  expect(tmux_ghostty.images == ava::tui::TerminalImageProtocol::None && tmux_ghostty.true_color && tmux_ghostty.hyperlinks && tmux_ghostty.badge == "tmux" &&
             tmux_ghostty.detail.find("tmux") != std::string::npos,
         "terminal image detection disables image protocols under tmux while preserving explicit truecolor and forwarded hyperlinks");

  auto const tmux_default = ava::tui::detect_terminal_image_capabilities(
      ava::tui::TerminalEnvironment{.term_program = "ghostty", .term = "tmux-256color", .color_term = "truecolor", .tmux = true});
  expect(tmux_default.images == ava::tui::TerminalImageProtocol::None && tmux_default.true_color && !tmux_default.hyperlinks &&
             tmux_default.detail.find("AVA_TUI_TMUX_HYPERLINKS") != std::string::npos,
         "terminal image detection keeps tmux OSC 8 disabled unless forwarding is explicitly advertised");

  {
    ScopedEnvVar term("TERM", "tmux-256color");
    ScopedEnvVar term_program("TERM_PROGRAM", "ghostty");
    ScopedEnvVar terminal_emulator("TERMINAL_EMULATOR", "");
    ScopedEnvVar color_term("COLORTERM", "truecolor");
    ScopedEnvVar tmux("TMUX", "/tmp/tmux-1000/default,123,0");
    ScopedEnvVar tmux_hyperlinks("AVA_TUI_TMUX_HYPERLINKS", "1");
    auto const tmux_env = ava::tui::current_terminal_environment();
    auto const tmux_env_caps = ava::tui::detect_terminal_image_capabilities(tmux_env);
    expect(tmux_env.tmux && tmux_env.tmux_forwards_hyperlinks && tmux_env_caps.images == ava::tui::TerminalImageProtocol::None && tmux_env_caps.hyperlinks &&
               tmux_env_caps.detail.find("explicit tmux forwarding") != std::string::npos,
           "terminal image detection honors the explicit tmux hyperlink forwarding environment hint at runtime");
  }

  auto const kitty = ava::tui::detect_terminal_image_capabilities(ava::tui::TerminalEnvironment{.term_program = "kitty", .term = "xterm-kitty"});
  auto const ghostty = ava::tui::detect_terminal_image_capabilities(ava::tui::TerminalEnvironment{.term_program = "ghostty"});
  auto const wezterm = ava::tui::detect_terminal_image_capabilities(ava::tui::TerminalEnvironment{.term_program = "wezterm"});
  auto const warp = ava::tui::detect_terminal_image_capabilities(ava::tui::TerminalEnvironment{.term_program = "warpterminal"});
  expect(kitty.images == ava::tui::TerminalImageProtocol::Kitty && ghostty.images == ava::tui::TerminalImageProtocol::Kitty &&
             wezterm.images == ava::tui::TerminalImageProtocol::Kitty && warp.images == ava::tui::TerminalImageProtocol::Kitty && kitty.true_color &&
             kitty.hyperlinks,
         "terminal image detection enables Kitty-compatible graphics for Kitty, Ghostty, WezTerm, and Warp outside multiplexers");

  auto const iterm = ava::tui::detect_terminal_image_capabilities(ava::tui::TerminalEnvironment{.term_program = "iterm.app"});
  auto const windows_terminal = ava::tui::detect_terminal_image_capabilities(ava::tui::TerminalEnvironment{.term = "xterm-256color", .wt_session = true});
  expect(iterm.images == ava::tui::TerminalImageProtocol::Iterm2 && iterm.true_color && iterm.hyperlinks &&
             windows_terminal.images == ava::tui::TerminalImageProtocol::None && windows_terminal.true_color && windows_terminal.hyperlinks,
         "terminal image detection distinguishes iTerm2 inline images from hyperlink-only terminals");

  auto const kitty_sequence = ava::tui::encode_kitty_image("AAAA", ava::tui::KittyImageOptions{.columns = 2, .rows = 2, .image_id = 42, .move_cursor = false});
  auto const long_kitty_sequence = ava::tui::encode_kitty_image(std::string(5000, 'A'));
  expect(kitty_sequence.starts_with("\x1b_Ga=T,f=100,q=2,C=1,c=2,r=2,i=42;AAAA") && kitty_sequence.ends_with("\x1b\\") &&
             long_kitty_sequence.find(",m=1;") != std::string::npos && long_kitty_sequence.find("\x1b_Gm=0;") != std::string::npos &&
             ava::tui::delete_kitty_image(42) == "\x1b_Ga=d,d=I,i=42,q=2\x1b\\" && ava::tui::delete_all_kitty_images() == "\x1b_Ga=d,d=A,q=2\x1b\\",
         "terminal image helpers build Kitty graphics and cleanup sequences without terminal-side cursor movement when requested");

  auto const iterm_sequence = ava::tui::encode_iterm2_image(
      "BBBB", ava::tui::Iterm2ImageOptions{.width = "12", .height = "auto", .name = "screen.png", .preserve_aspect_ratio = false});
  expect(iterm_sequence.starts_with("\x1b]1337;File=inline=1;width=12;height=auto;name=c2NyZWVuLnBuZw==;preserveAspectRatio=0:BBBB") &&
             iterm_sequence.ends_with("\a"),
         "terminal image helpers build iTerm2 inline image sequences with encoded names");

  expect(ava::tui::terminal_line_contains_image_sequence("plain text") == false &&
             ava::tui::terminal_line_contains_image_sequence(std::string("x") + "\x1b_Gpayload\x1b\\") &&
             ava::tui::terminal_line_contains_image_sequence(std::string("x") + "\x1b]1337;File=inline=1:AAAA\a"),
         "terminal image line detection recognizes Kitty and iTerm2 escape sequences without false positives on plain paths");

  auto const cells = ava::tui::calculate_image_cell_size(ava::tui::ImageDimensions{.width_px = 20, .height_px = 100}, 10, 5,
                                                         ava::tui::TerminalCellDimensions{.width_px = 10, .height_px = 10});
  expect(cells.columns == 1 && cells.rows == 5, "terminal image sizing preserves aspect ratio within cell bounds");

  auto const fallback_cell_size = ava::tui::calculate_image_cell_size(ava::tui::ImageDimensions{.width_px = 90, .height_px = 90}, 10);
  auto const square_cell_size = ava::tui::calculate_image_cell_size(ava::tui::ImageDimensions{.width_px = 90, .height_px = 90}, 10, std::nullopt,
                                                                    ava::tui::TerminalCellDimensions{.width_px = 9, .height_px = 9});
  expect(fallback_cell_size.columns == 10 && fallback_cell_size.rows == 5 && square_cell_size.columns == 10 && square_cell_size.rows == 10,
         "terminal image sizing documents the 9x18 fallback cell constraint and honors explicit cell dimensions when supplied");

  std::string png(24, '\0');
  png[0] = static_cast<char>(0x89);
  png[1] = 'P';
  png[2] = 'N';
  png[3] = 'G';
  png[4] = '\r';
  png[5] = '\n';
  png[6] = static_cast<char>(0x1A);
  png[7] = '\n';
  png[11] = 13;
  png[12] = 'I';
  png[13] = 'H';
  png[14] = 'D';
  png[15] = 'R';
  png[16] = 0;
  png[17] = 0;
  png[18] = 1;
  png[19] = 44;
  png[20] = 0;
  png[21] = 0;
  png[22] = 0;
  png[23] = static_cast<char>(200);
  auto const dimensions = ava::tui::image_dimensions_from_bytes(png, "image/png");
  expect(dimensions && dimensions->width_px == 300 && dimensions->height_px == 200,
         "terminal image dimension parser reads PNG dimensions for future preview row sizing");
}
}  // namespace

void run_tui_terminal_image_tests()
{
  test_tui_terminal_image_support();
}

void run_tui_terminal_input_tests_part_1()
{
  expect(ava::tui::terminal_escape_delay_ms() == 100,
         "terminal escape delay is deliberately tuned low enough for responsive Esc while buffering split CSI input");
  auto const osc8_width_sample = std::string("a\x1b]8;;https://example.test\x1b\\b\x1b]8;;\x1b\\");
  expect(ava::tui::detail::terminal_text_columns(osc8_width_sample) == 2 &&
             ava::tui::detail::fit_line_preserving_sgr(std::string("\x1b]8;;https://example.test\x1b\\abcdef\x1b]8;;\x1b\\"), 4).find("\x1b]8;;\x1b\\...") !=
                 std::string::npos,
         "tui width helpers treat OSC 8 hyperlinks as zero-width and close truncated links before ellipses");
  auto const underlined_wrap = ava::tui::detail::wrap_ansi_text(std::string(ava::tui::detail::kSgrUnderline) + "abcdef", 3);
  expect(underlined_wrap.size() == 2 && underlined_wrap[0] == std::string(ava::tui::detail::kSgrUnderline) + "abc" + std::string(ava::tui::detail::kSgrReset) &&
             underlined_wrap[1] == std::string(ava::tui::detail::kSgrUnderline) + "def" + std::string(ava::tui::detail::kSgrReset),
         "tui ANSI wrapping closes active underline at synthetic line breaks and reopens it on continuations");
  auto const reset_before_wrap =
      ava::tui::detail::wrap_ansi_text(std::string(ava::tui::detail::kSgrUnderline) + "abc" + std::string(ava::tui::detail::kSgrReset) + "def", 3);
  expect(reset_before_wrap.size() == 2 && reset_before_wrap[1] == "def", "tui ANSI wrapping honors explicit SGR resets before continuing with plain text");
  auto const background_sgr = std::string("\x1b[48;2;1;2;3m");
  auto const background_wrap = ava::tui::detail::wrap_ansi_text(background_sgr + "abcd" + std::string(ava::tui::detail::kSgrReset), 2);
  expect(background_wrap.size() == 2 && background_wrap[0] == background_sgr + "ab" + std::string(ava::tui::detail::kSgrReset) &&
             background_wrap[1] == background_sgr + "cd" + std::string(ava::tui::detail::kSgrReset),
         "tui ANSI wrapping preserves truecolor backgrounds across wrapped rows when the background remains active");
  auto const long_word_wrap = ava::tui::detail::wrap_ansi_text("abcdef", 2);
  expect(long_word_wrap == std::vector<std::string>({"ab", "cd", "ef"}), "tui ANSI wrapping hard-wraps long unbroken words deterministically");
  auto const cjk_ansi_wrap = ava::tui::detail::wrap_ansi_text(std::string("a") + "\xE7\x95\x8C" + "\xE7\x95\x8C" + "b", 3);
  expect(cjk_ansi_wrap.size() == 2 && cjk_ansi_wrap[0] == std::string("a") + "\xE7\x95\x8C" && cjk_ansi_wrap[1] == std::string("\xE7\x95\x8C") + "b" &&
             ava::tui::detail::terminal_text_columns(cjk_ansi_wrap[0]) == 3 && ava::tui::detail::terminal_text_columns(cjk_ansi_wrap[1]) == 3,
         "tui ANSI wrapping uses AVA display-width accounting for CJK cells");
  auto const newline_wrap = ava::tui::detail::wrap_ansi_text("ab\r\ncd\nef", 2);
  expect(newline_wrap == std::vector<std::string>({"ab", "cd", "ef"}), "tui ANSI wrapping treats CRLF and LF as explicit line breaks");
  expect(ava::tui::detail::wrap_ansi_text("", 4) == std::vector<std::string>({""}), "tui ANSI wrapping returns one empty row for empty input");
  expect(ava::tui::detail::wrap_ansi_text("ab", 0) == std::vector<std::string>({"a", "b"}), "tui ANSI wrapping clamps zero width to one column");
  auto const osc_open = std::string("\x1b]8;;https://example.test\x1b\\");
  auto const osc_close = std::string("\x1b]8;;\x1b\\");
  auto const osc_wrap = ava::tui::detail::wrap_ansi_text(osc_open + "abcd" + osc_close, 2);
  expect(osc_wrap.size() == 2 && osc_wrap[0] == osc_open + "ab" + osc_close && osc_wrap[1] == osc_open + "cd" + osc_close,
         "tui ANSI wrapping closes and reopens OSC 8 hyperlinks at synthetic line breaks");
  auto const malformed_color_wrap = ava::tui::detail::wrap_ansi_text(std::string("\x1b[38;2m") + "ab", 1);
  expect(malformed_color_wrap == std::vector<std::string>({std::string("\x1b[38;2m") + "a", "b"}),
         "tui ANSI wrapping preserves malformed extended color bytes without leaking dim state");
  expect(ava::tui::detail::ncurses_color_role_for_sgr(ava::tui::detail::kSgrMuted) == ava::tui::detail::NcursesColorRole::Muted &&
             ava::tui::detail::ncurses_color_role_for_sgr(ava::tui::detail::kSgrThinking) == ava::tui::detail::NcursesColorRole::Muted &&
             ava::tui::detail::ncurses_color_role_for_sgr(ava::tui::detail::kSgrTextDimmed) == ava::tui::detail::NcursesColorRole::Muted &&
             ava::tui::detail::ncurses_color_role_for_sgr(ava::tui::detail::kSgrAccent) == ava::tui::detail::NcursesColorRole::Accent,
         "tui ncurses SGR roles classify AVA muted and thinking colors before the blue accent");
  auto const sanitized_wrap = ava::tui::detail::wrap_transcript_text(std::string(ava::tui::detail::kSgrUnderline) + "ab", 3);
  expect(!sanitized_wrap.empty() && sanitized_wrap[0].find('\x1b') == std::string::npos,
         "production transcript wrapping still sanitizes raw escape sequences before wrapping");
  expect(ava::tui::terminal_kitty_keyboard_push_sequence() == std::string_view("\x1b[>5u") &&
             ava::tui::terminal_kitty_keyboard_query_sequence() == std::string_view("\x1b[>5u\x1b[?u\x1b[c") &&
             ava::tui::terminal_kitty_keyboard_pop_sequence() == std::string_view("\x1b[<u"),
         "terminal session requests Kitty keyboard disambiguation plus alternate-key reporting, queries support, and restores the stack on exit");
  expect(ava::tui::terminal_modify_other_keys_enable_sequence() == std::string_view("\x1b[>4;2m") &&
             ava::tui::terminal_modify_other_keys_disable_sequence() == std::string_view("\x1b[>4;0m"),
         "terminal session has xterm modifyOtherKeys fallback enable and disable sequences");
  auto const kitty_flags = ava::tui::terminal_kitty_keyboard_flags_response("[?5u");
  auto const kitty_zero_flags = ava::tui::terminal_kitty_keyboard_flags_response("[?0u");
  using KeyboardAction = ava::tui::KeyboardProtocolResponseAction;
  expect(kitty_flags && *kitty_flags == 5 && kitty_zero_flags && *kitty_zero_flags == 0 && !ava::tui::terminal_kitty_keyboard_flags_response("[?u") &&
             !ava::tui::terminal_kitty_keyboard_flags_response("[?5c") && ava::tui::terminal_device_attributes_response("[?1c") &&
             ava::tui::terminal_device_attributes_response("[?62;4;52c") && !ava::tui::terminal_device_attributes_response("[?c") &&
             !ava::tui::terminal_device_attributes_response("[?62;4;52u") &&
             ava::tui::terminal_keyboard_protocol_response_action("[?0u", false, false) == KeyboardAction::EnableModifyOtherKeys &&
             ava::tui::terminal_keyboard_protocol_response_action("[?62;4;52c", false, false) == KeyboardAction::EnableModifyOtherKeys &&
             ava::tui::terminal_keyboard_protocol_response_action("[?62;4;52c", true, false) == KeyboardAction::None &&
             ava::tui::terminal_keyboard_protocol_response_action("[?5u", false, true) == KeyboardAction::DisableModifyOtherKeys &&
             ava::tui::terminal_keyboard_protocol_response_action("[?5u", false, false) == KeyboardAction::None,
         "terminal keyboard negotiation parser recognizes Kitty flag and device-attribute replies and derives fallback actions");
  expect(
      ava::tui::terminal_escape_sequence_key("[27;2;13~") == ava::tui::Key::ShiftEnter &&
          ava::tui::terminal_escape_sequence_key("[13;2u") == ava::tui::Key::ShiftEnter &&
          ava::tui::terminal_escape_sequence_key("[13;2~") == ava::tui::Key::ShiftEnter &&
          ava::tui::terminal_escape_sequence_key("[27;5;13~") == ava::tui::Key::CtrlEnter &&
          ava::tui::terminal_escape_sequence_key("[13;5u") == ava::tui::Key::CtrlEnter &&
          ava::tui::terminal_escape_sequence_key("[13;5~") == ava::tui::Key::CtrlEnter &&
          ava::tui::terminal_escape_sequence_key("[27;3;13~") == ava::tui::Key::AltEnter &&
          ava::tui::terminal_escape_sequence_key("[13;3u") == ava::tui::Key::AltEnter &&
          ava::tui::terminal_escape_sequence_key("[13;3~") == ava::tui::Key::AltEnter &&
          ava::tui::terminal_escape_sequence_key("\r") == ava::tui::Key::AltEnter && ava::tui::terminal_escape_sequence_complete("\r") &&
          ava::tui::terminal_escape_sequence_key("[Z") == ava::tui::Key::ShiftTab &&
          ava::tui::terminal_escape_sequence_key("[1;6P") == ava::tui::Key::CtrlShiftP &&
          ava::tui::terminal_escape_sequence_key("[80;6u") == ava::tui::Key::CtrlShiftP &&
          ava::tui::terminal_escape_sequence_key("[1079::112;6u") == ava::tui::Key::CtrlShiftP &&
          ava::tui::terminal_escape_sequence_key("[103;5u") == ava::tui::Key::CtrlG && ava::tui::terminal_escape_sequence_key("[3~") == ava::tui::Key::Delete &&
          ava::tui::terminal_escape_sequence_key("[3$") == ava::tui::Key::ShiftDelete && ava::tui::terminal_escape_sequence_complete("[3$") &&
          ava::tui::terminal_escape_sequence_key("[3;2~") == ava::tui::Key::ShiftDelete &&
          ava::tui::terminal_escape_sequence_key("[2~") == ava::tui::Key::Insert && ava::tui::terminal_escape_sequence_key("[5~") == ava::tui::Key::PageUp &&
          ava::tui::terminal_escape_sequence_key("[6~") == ava::tui::Key::PageDown &&
          ava::tui::terminal_escape_sequence_key("[5;2~") == ava::tui::Key::PageUp &&
          ava::tui::terminal_escape_sequence_key("[6;2~") == ava::tui::Key::PageDown && ava::tui::terminal_escape_sequence_key("OP") == ava::tui::Key::F1 &&
          ava::tui::terminal_escape_sequence_key("OQ") == ava::tui::Key::F2 && ava::tui::terminal_escape_sequence_key("[11~") == ava::tui::Key::F1 &&
          ava::tui::terminal_escape_sequence_key("[12~") == ava::tui::Key::F2 && ava::tui::terminal_escape_sequence_key("[24~") == ava::tui::Key::F12 &&
          ava::tui::terminal_escape_sequence_key("[H") == ava::tui::Key::Home && ava::tui::terminal_escape_sequence_key("OH") == ava::tui::Key::Home &&
          ava::tui::terminal_escape_sequence_key("[1~") == ava::tui::Key::Home && ava::tui::terminal_escape_sequence_key("[7~") == ava::tui::Key::Home &&
          ava::tui::terminal_escape_sequence_key("[1;5H") == ava::tui::Key::CtrlHome &&
          ava::tui::terminal_escape_sequence_key("[7;5~") == ava::tui::Key::CtrlHome &&
          ava::tui::terminal_escape_sequence_key("[1;6H") == ava::tui::Key::ShiftCtrlHome &&
          ava::tui::terminal_escape_sequence_key("[7;6~") == ava::tui::Key::ShiftCtrlHome &&
          ava::tui::terminal_escape_sequence_key("[1;2H") == ava::tui::Key::ShiftHome &&
          ava::tui::terminal_escape_sequence_key("[2H") == ava::tui::Key::ShiftHome &&
          ava::tui::terminal_escape_sequence_key("[7$") == ava::tui::Key::ShiftHome &&
          ava::tui::terminal_escape_sequence_key("[7;2~") == ava::tui::Key::ShiftHome && ava::tui::terminal_escape_sequence_key("[F") == ava::tui::Key::End &&
          ava::tui::terminal_escape_sequence_key("OF") == ava::tui::Key::End && ava::tui::terminal_escape_sequence_key("[4~") == ava::tui::Key::End &&
          ava::tui::terminal_escape_sequence_key("[8~") == ava::tui::Key::End && ava::tui::terminal_escape_sequence_key("[1;5F") == ava::tui::Key::CtrlEnd &&
          ava::tui::terminal_escape_sequence_key("[8;5~") == ava::tui::Key::CtrlEnd &&
          ava::tui::terminal_escape_sequence_key("[1;6F") == ava::tui::Key::ShiftCtrlEnd &&
          ava::tui::terminal_escape_sequence_key("[8;6~") == ava::tui::Key::ShiftCtrlEnd &&
          ava::tui::terminal_escape_sequence_key("[1;2F") == ava::tui::Key::ShiftEnd &&
          ava::tui::terminal_escape_sequence_key("[2F") == ava::tui::Key::ShiftEnd &&
          ava::tui::terminal_escape_sequence_key("[8$") == ava::tui::Key::ShiftEnd &&
          ava::tui::terminal_escape_sequence_key("[8;2~") == ava::tui::Key::ShiftEnd &&
          ava::tui::terminal_escape_sequence_key("[45;5u") == ava::tui::Key::CtrlMinus &&
          ava::tui::terminal_escape_sequence_key(std::string_view("\x1f", 1)) == ava::tui::Key::CtrlMinus &&
          ava::tui::terminal_escape_sequence_key(std::string_view("\x1d", 1)) == ava::tui::Key::CtrlAltRightBracket &&
          ava::tui::terminal_escape_sequence_key("[3;3~") == ava::tui::Key::AltDelete &&
          ava::tui::terminal_escape_sequence_key("[1;5D") == ava::tui::Key::CtrlArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[1;5C") == ava::tui::Key::CtrlArrowRight &&
          ava::tui::terminal_escape_sequence_key("[5D") == ava::tui::Key::CtrlArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[5C") == ava::tui::Key::CtrlArrowRight &&
          ava::tui::terminal_escape_sequence_key("[1;2D") == ava::tui::Key::ShiftArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[1;2C") == ava::tui::Key::ShiftArrowRight &&
          ava::tui::terminal_escape_sequence_key("[2D") == ava::tui::Key::ShiftArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[2C") == ava::tui::Key::ShiftArrowRight &&
          ava::tui::terminal_escape_sequence_key("[d") == ava::tui::Key::ShiftArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[c") == ava::tui::Key::ShiftArrowRight &&
          ava::tui::terminal_escape_sequence_key("[1;2A") == ava::tui::Key::ShiftArrowUp &&
          ava::tui::terminal_escape_sequence_key("[1;2B") == ava::tui::Key::ShiftArrowDown &&
          ava::tui::terminal_escape_sequence_key("[2A") == ava::tui::Key::ShiftArrowUp &&
          ava::tui::terminal_escape_sequence_key("[2B") == ava::tui::Key::ShiftArrowDown &&
          ava::tui::terminal_escape_sequence_key("[a") == ava::tui::Key::ShiftArrowUp &&
          ava::tui::terminal_escape_sequence_key("[b") == ava::tui::Key::ShiftArrowDown &&
          ava::tui::terminal_escape_sequence_key("[1;6D") == ava::tui::Key::ShiftCtrlArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[1;6C") == ava::tui::Key::ShiftCtrlArrowRight &&
          ava::tui::terminal_escape_sequence_key("[6D") == ava::tui::Key::ShiftCtrlArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[6C") == ava::tui::Key::ShiftCtrlArrowRight &&
          ava::tui::terminal_escape_sequence_key("[1;4D") == ava::tui::Key::ShiftAltArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[1;4C") == ava::tui::Key::ShiftAltArrowRight &&
          ava::tui::terminal_escape_sequence_key("[4D") == ava::tui::Key::ShiftAltArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[4C") == ava::tui::Key::ShiftAltArrowRight &&
          ava::tui::terminal_escape_sequence_key("[1;3A") == ava::tui::Key::AltArrowUp &&
          ava::tui::terminal_escape_sequence_key("[3A") == ava::tui::Key::AltArrowUp &&
          ava::tui::terminal_escape_sequence_key("[1;3B") == ava::tui::Key::AltArrowDown &&
          ava::tui::terminal_escape_sequence_key("[3B") == ava::tui::Key::AltArrowDown &&
          ava::tui::terminal_escape_sequence_key("[1;3D") == ava::tui::Key::AltArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[1;3C") == ava::tui::Key::AltArrowRight &&
          ava::tui::terminal_escape_sequence_key("[3D") == ava::tui::Key::AltArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[3C") == ava::tui::Key::AltArrowRight &&
          ava::tui::terminal_escape_sequence_key("[A") == ava::tui::Key::ArrowUp && ava::tui::terminal_escape_sequence_key("[B") == ava::tui::Key::ArrowDown &&
          ava::tui::terminal_escape_sequence_key("[C") == ava::tui::Key::ArrowRight &&
          ava::tui::terminal_escape_sequence_key("[D") == ava::tui::Key::ArrowLeft && ava::tui::terminal_escape_sequence_key("OA") == ava::tui::Key::ArrowUp &&
          ava::tui::terminal_escape_sequence_key("OB") == ava::tui::Key::ArrowDown &&
          ava::tui::terminal_escape_sequence_key("OC") == ava::tui::Key::ArrowRight &&
          ava::tui::terminal_escape_sequence_key("OD") == ava::tui::Key::ArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[1;1A") == ava::tui::Key::ArrowUp &&
          ava::tui::terminal_escape_sequence_key("[1;1B") == ava::tui::Key::ArrowDown &&
          ava::tui::terminal_escape_sequence_key("[1;1C") == ava::tui::Key::ArrowRight &&
          ava::tui::terminal_escape_sequence_key("[1;1D") == ava::tui::Key::ArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[57417u") == ava::tui::Key::ArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[57418u") == ava::tui::Key::ArrowRight &&
          ava::tui::terminal_escape_sequence_key("[57419u") == ava::tui::Key::ArrowUp &&
          ava::tui::terminal_escape_sequence_key("[57420u") == ava::tui::Key::ArrowDown &&
          ava::tui::terminal_escape_sequence_key("[57419;2u") == ava::tui::Key::ShiftArrowUp &&
          ava::tui::terminal_escape_sequence_key("[57420;2u") == ava::tui::Key::ShiftArrowDown &&
          ava::tui::terminal_escape_sequence_key("[57417;2u") == ava::tui::Key::ShiftArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[57418;2u") == ava::tui::Key::ShiftArrowRight &&
          ava::tui::terminal_escape_sequence_key("[57417;5u") == ava::tui::Key::CtrlArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[57418;5u") == ava::tui::Key::CtrlArrowRight &&
          ava::tui::terminal_escape_sequence_key("[57417;3u") == ava::tui::Key::AltArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[57418;3u") == ava::tui::Key::AltArrowRight &&
          ava::tui::terminal_escape_sequence_key("[57417;6u") == ava::tui::Key::ShiftCtrlArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[57418;6u") == ava::tui::Key::ShiftCtrlArrowRight &&
          ava::tui::terminal_escape_sequence_key("[57417;4u") == ava::tui::Key::ShiftAltArrowLeft &&
          ava::tui::terminal_escape_sequence_key("[57418;4u") == ava::tui::Key::ShiftAltArrowRight &&
          ava::tui::terminal_escape_sequence_key("[57419;3u") == ava::tui::Key::AltArrowUp &&
          ava::tui::terminal_escape_sequence_key("[57420;3u") == ava::tui::Key::AltArrowDown &&
          ava::tui::terminal_escape_sequence_key("[57421u") == ava::tui::Key::PageUp &&
          ava::tui::terminal_escape_sequence_key("[57422u") == ava::tui::Key::PageDown &&
          ava::tui::terminal_escape_sequence_key("[57423u") == ava::tui::Key::Home && ava::tui::terminal_escape_sequence_key("[57424u") == ava::tui::Key::End &&
          ava::tui::terminal_escape_sequence_key("[57423;5u") == ava::tui::Key::CtrlHome &&
          ava::tui::terminal_escape_sequence_key("[57424;5u") == ava::tui::Key::CtrlEnd &&
          ava::tui::terminal_escape_sequence_key("[57423;6u") == ava::tui::Key::ShiftCtrlHome &&
          ava::tui::terminal_escape_sequence_key("[57424;6u") == ava::tui::Key::ShiftCtrlEnd &&
          ava::tui::terminal_escape_sequence_key("[57423;2u") == ava::tui::Key::ShiftHome &&
          ava::tui::terminal_escape_sequence_key("[57424;2u") == ava::tui::Key::ShiftEnd &&
          ava::tui::terminal_escape_sequence_key("[57426u") == ava::tui::Key::Delete &&
          ava::tui::terminal_escape_sequence_key("[57426;2u") == ava::tui::Key::ShiftDelete &&
          ava::tui::terminal_escape_sequence_key("[57426;3u") == ava::tui::Key::AltDelete &&
          ava::tui::terminal_escape_sequence_key("[127;2u") == ava::tui::Key::ShiftBackspace &&
          ava::tui::terminal_escape_sequence_key("[127;5u") == ava::tui::Key::CtrlBackspace &&
          ava::tui::terminal_escape_sequence_key("[99;5u") == ava::tui::Key::CtrlC &&
          ava::tui::terminal_escape_sequence_key("[120;5u") == ava::tui::Key::CtrlX &&
          ava::tui::terminal_escape_sequence_key("[1089::99;5u") == ava::tui::Key::CtrlC &&
          ava::tui::terminal_escape_sequence_key("[99;5:2u") == ava::tui::Key::CtrlC &&
          ava::tui::terminal_escape_sequence_key("[99;5:3u") == ava::tui::Key::Unknown &&
          ava::tui::terminal_escape_sequence_key("[100;3u") == ava::tui::Key::AltD &&
          ava::tui::terminal_escape_sequence_key("[27;5;99~") == ava::tui::Key::CtrlC &&
          ava::tui::terminal_escape_sequence_key("[27;5;100~") == ava::tui::Key::CtrlD &&
          ava::tui::terminal_escape_sequence_key("[27;5;45~") == ava::tui::Key::CtrlMinus &&
          ava::tui::terminal_escape_sequence_key("[27;6;80~") == ava::tui::Key::CtrlShiftP &&
          ava::tui::terminal_escape_sequence_key("[27;3;100~") == ava::tui::Key::AltD &&
          ava::tui::terminal_escape_sequence_key("[27;2;127~") == ava::tui::Key::ShiftBackspace &&
          ava::tui::terminal_escape_sequence_key("[27;5;127~") == ava::tui::Key::CtrlBackspace &&
          ava::tui::terminal_escape_sequence_key("[27;3;127~") == ava::tui::Key::AltBackspace &&
          ava::tui::terminal_escape_sequence_key("[27;1;127~") == ava::tui::Key::Backspace &&
          ava::tui::terminal_escape_sequence_key("[27;1;27~") == ava::tui::Key::Escape &&
          ava::tui::terminal_escape_sequence_key(std::string_view("\x00", 1)) == ava::tui::Key::CtrlSpace &&
          ava::tui::terminal_escape_sequence_key("[32;5u") == ava::tui::Key::CtrlSpace &&
          ava::tui::terminal_escape_sequence_key("[27;5;32~") == ava::tui::Key::CtrlSpace &&
          ava::tui::terminal_escape_sequence_key("[48;5u") == ava::tui::Key::Ctrl0 &&
          ava::tui::terminal_escape_sequence_key("[49;5u") == ava::tui::Key::Ctrl1 &&
          ava::tui::terminal_escape_sequence_key("[57;5u") == ava::tui::Key::Ctrl9 &&
          ava::tui::terminal_escape_sequence_key("[27;5;49~") == ava::tui::Key::Ctrl1 &&
          ava::tui::terminal_escape_sequence_key("[47;5u") == ava::tui::Key::CtrlSlash &&
          ava::tui::terminal_escape_sequence_key("[47::91;5u") == ava::tui::Key::CtrlSlash &&
          ava::tui::terminal_escape_sequence_key("[27;5;47~") == ava::tui::Key::CtrlSlash &&
          ava::tui::terminal_escape_sequence_key("[27;2;9~") == ava::tui::Key::ShiftTab &&
          ava::tui::terminal_escape_sequence_key("[<64;12;9M") == ava::tui::Key::MouseWheelUp &&
          ava::tui::terminal_escape_sequence_key("[<65;12;9M") == ava::tui::Key::MouseWheelDown &&
          ava::tui::terminal_escape_sequence_key("[<68;12;9M") == ava::tui::Key::MouseWheelUp &&
          ava::tui::terminal_escape_sequence_key("[<69;12;9M") == ava::tui::Key::MouseWheelDown &&
          ava::tui::terminal_escape_sequence_key("[<0;12;9M") == ava::tui::Key::MouseLeftClick &&
          ava::tui::terminal_escape_sequence_key("[<32;12;9M") == ava::tui::Key::MouseLeftDrag &&
          ava::tui::terminal_escape_sequence_key("[<64;12;9m") == ava::tui::Key::Unknown &&
          ava::tui::terminal_escape_sequence_key("[<65;12;9m") == ava::tui::Key::Unknown &&
          ava::tui::terminal_escape_sequence_key("[<0;12;9m") == ava::tui::Key::MouseLeftRelease &&
          ava::tui::terminal_escape_sequence_key(std::string_view("\x7f", 1)) == ava::tui::Key::AltBackspace &&
          ava::tui::terminal_escape_sequence_key(std::string_view("\b", 1)) == ava::tui::Key::AltBackspace &&
          ava::tui::terminal_escape_sequence_key("b") == ava::tui::Key::AltB && ava::tui::terminal_escape_sequence_key("d") == ava::tui::Key::AltD &&
          ava::tui::terminal_escape_sequence_key("f") == ava::tui::Key::AltF && ava::tui::terminal_escape_sequence_key("h") == ava::tui::Key::AltH &&
          ava::tui::terminal_escape_sequence_key("j") == ava::tui::Key::AltJ && ava::tui::terminal_escape_sequence_key("k") == ava::tui::Key::AltK &&
          ava::tui::terminal_escape_sequence_key("l") == ava::tui::Key::AltL && ava::tui::terminal_escape_sequence_key("w") == ava::tui::Key::AltW &&
          ava::tui::terminal_escape_sequence_key("y") == ava::tui::Key::AltY && ava::tui::terminal_escape_sequence_key("[13;2") == ava::tui::Key::Unknown &&
          ava::tui::terminal_escape_sequence_key("[13;5") == ava::tui::Key::Unknown &&
          ava::tui::terminal_escape_sequence_key("[27;2;13") == ava::tui::Key::Unknown &&
          ava::tui::terminal_escape_sequence_key("[999999999999999999999;2u") == ava::tui::Key::Unknown &&
          ava::tui::terminal_escape_sequence_key("[200~") == ava::tui::Key::Unknown,
      "terminal escape parser maps complete modified Enter CSI forms without treating partial keys or paste markers as "
      "text");
  expect(ava::tui::terminal_escape_sequence_key("[1;129B") == ava::tui::Key::ArrowDown &&
             ava::tui::terminal_escape_sequence_key("[1;193A") == ava::tui::Key::ArrowUp &&
             ava::tui::terminal_escape_sequence_key("[1;130A") == ava::tui::Key::ShiftArrowUp &&
             ava::tui::terminal_escape_sequence_key("[1;133D") == ava::tui::Key::CtrlArrowLeft &&
             ava::tui::terminal_escape_sequence_key("[1;17B") == ava::tui::Key::Unknown &&
             ava::tui::terminal_escape_sequence_key("[57420;129u") == ava::tui::Key::ArrowDown &&
             ava::tui::terminal_escape_sequence_key("[5;129~") == ava::tui::Key::PageUp &&
             ava::tui::terminal_escape_sequence_key("[6;129~") == ava::tui::Key::PageDown &&
             ava::tui::terminal_escape_sequence_key("[1;129H") == ava::tui::Key::Home &&
             ava::tui::terminal_escape_sequence_key("[1;129F") == ava::tui::Key::End &&
             ava::tui::terminal_escape_sequence_key("[1;130H") == ava::tui::Key::ShiftHome &&
             ava::tui::terminal_escape_sequence_key("[1;133F") == ava::tui::Key::CtrlEnd &&
             ava::tui::terminal_escape_sequence_key("[3;129~") == ava::tui::Key::Delete &&
             ava::tui::terminal_escape_sequence_key("[3;130~") == ava::tui::Key::ShiftDelete &&
             ava::tui::terminal_escape_sequence_key("[3;131~") == ava::tui::Key::AltDelete,
         "terminal escape parser ignores Ghostty Kitty lock modifiers for physical navigation keys");
  auto const kitty_keypad_one = ava::tui::terminal_escape_sequence_event("[57400u");
  auto const kitty_keypad_plus = ava::tui::terminal_escape_sequence_event("[57413u");
  auto const kitty_shifted_letter = ava::tui::terminal_escape_sequence_event("[97:65:97;2u");
  auto const kitty_repeat_letter = ava::tui::terminal_escape_sequence_event("[97;1:2u");
  auto const kitty_release_letter = ava::tui::terminal_escape_sequence_event("[97;1:3u");
  auto const modify_plain_letter = ava::tui::terminal_escape_sequence_event("[27;1;120~");
  auto const modify_shifted_letter = ava::tui::terminal_escape_sequence_event("[27;2;69~");
  expect(kitty_keypad_one.key == ava::tui::Key::Character && kitty_keypad_one.text == "1" && kitty_keypad_plus.key == ava::tui::Key::Character &&
             kitty_keypad_plus.text == "+" && kitty_shifted_letter.key == ava::tui::Key::Character && kitty_shifted_letter.text == "A" &&
             kitty_repeat_letter.key == ava::tui::Key::Character && kitty_repeat_letter.text == "a" && kitty_release_letter.key == ava::tui::Key::Unknown &&
             modify_plain_letter.key == ava::tui::Key::Character && modify_plain_letter.text == "x" && modify_shifted_letter.key == ava::tui::Key::Character &&
             modify_shifted_letter.text == "E",
         "terminal escape parser decodes Kitty CSI-u and xterm modifyOtherKeys printable reports while ignoring releases");
  auto const legacy_mouse_sequence = [](unsigned int button, unsigned int column, unsigned int row) {
    std::string sequence = "[M";
    sequence.push_back(static_cast<char>(button + 32U));
    sequence.push_back(static_cast<char>(column + 32U));
    sequence.push_back(static_cast<char>(row + 32U));
    return sequence;
  };
  auto const sgr_click = ava::tui::terminal_escape_sequence_event("[<0;12;9M");
  auto const sgr_release = ava::tui::terminal_escape_sequence_event("[<0;12;9m");
  auto const sgr_drag = ava::tui::terminal_escape_sequence_event("[<32;12;9M");
  auto const sgr_wheel = ava::tui::terminal_escape_sequence_event("[<65;21;7M");
  auto const legacy_click = ava::tui::terminal_escape_sequence_event(legacy_mouse_sequence(0, 12, 9));
  auto const legacy_release = ava::tui::terminal_escape_sequence_event(legacy_mouse_sequence(3, 12, 9));
  auto const legacy_drag = ava::tui::terminal_escape_sequence_event(legacy_mouse_sequence(32, 12, 9));
  auto const legacy_wheel = ava::tui::terminal_escape_sequence_event(legacy_mouse_sequence(64, 21, 7));
  expect(sgr_click.key == ava::tui::Key::MouseLeftClick && sgr_click.mouse_column == 12 && sgr_click.mouse_row == 9 &&
             sgr_release.key == ava::tui::Key::MouseLeftRelease && sgr_release.mouse_column == 12 && sgr_release.mouse_row == 9 &&
             sgr_drag.key == ava::tui::Key::MouseLeftDrag && sgr_drag.mouse_column == 12 && sgr_drag.mouse_row == 9 &&
             sgr_wheel.key == ava::tui::Key::MouseWheelDown && sgr_wheel.mouse_column == 21 && sgr_wheel.mouse_row == 7 &&
             legacy_click.key == ava::tui::Key::MouseLeftClick && legacy_click.mouse_column == 12 && legacy_click.mouse_row == 9 &&
             legacy_release.key == ava::tui::Key::MouseLeftRelease && legacy_release.mouse_column == 12 && legacy_release.mouse_row == 9 &&
             legacy_drag.key == ava::tui::Key::MouseLeftDrag && legacy_drag.mouse_column == 12 && legacy_drag.mouse_row == 9 &&
             legacy_wheel.key == ava::tui::Key::MouseWheelUp && legacy_wheel.mouse_column == 21 && legacy_wheel.mouse_row == 7,
         "terminal escape parser preserves SGR and legacy mouse click, drag, release, and wheel coordinates");
  expect(ava::tui::terminal_escape_sequence_complete("[13;2u") && ava::tui::terminal_escape_sequence_complete("[?25l") &&
             ava::tui::terminal_escape_sequence_complete("[45;5u") && ava::tui::terminal_escape_sequence_complete("[3;3~") &&
             ava::tui::terminal_escape_sequence_complete("[<0;12;9M") && ava::tui::terminal_escape_sequence_complete(legacy_mouse_sequence(0, 12, 9)) &&
             ava::tui::terminal_escape_sequence_complete(std::string_view("\x00", 1)) &&
             ava::tui::terminal_escape_sequence_complete(std::string_view("\x1f", 1)) &&
             ava::tui::terminal_escape_sequence_complete(std::string_view("\x1d", 1)) &&
             ava::tui::terminal_escape_sequence_complete(std::string_view("\x7f", 1)) &&
             ava::tui::terminal_escape_sequence_complete(std::string_view("\b", 1)) &&
             ava::tui::terminal_escape_sequence_complete(std::string("]0;AVA") + "\a") &&
             ava::tui::terminal_escape_sequence_complete(std::string("]52;c;AAAA") + "\x1b\\") &&
             ava::tui::terminal_escape_sequence_complete(std::string("P1;2|payload") + "\x1b\\") && !ava::tui::terminal_escape_sequence_complete("[13;2") &&
             !ava::tui::terminal_escape_sequence_complete("[200") && !ava::tui::terminal_escape_sequence_complete("[M ") &&
             !ava::tui::terminal_escape_sequence_complete("]0;AVA") && !ava::tui::terminal_escape_sequence_complete(std::string("P1;2|payload")),
         "terminal escape parser buffers CSI, OSC, and DCS sequences until a real terminator is present");
  expect(ava::tui::terminal_escape_sequence_should_discard("[?25l") && ava::tui::terminal_escape_sequence_should_discard("[?5u") &&
             ava::tui::terminal_escape_sequence_should_discard("[?62;4;52c") &&
             ava::tui::terminal_escape_sequence_should_discard(std::string("]0;AVA") + "\a") &&
             ava::tui::terminal_escape_sequence_should_discard(std::string("]52;c;AAAA") + "\x1b\\") &&
             ava::tui::terminal_escape_sequence_should_discard(std::string("P1;2|payload") + "\x1b\\") &&
             ava::tui::terminal_escape_sequence_should_discard("[<64;12;9m") && ava::tui::terminal_escape_sequence_should_discard("[97;1:3u") &&
             !ava::tui::terminal_escape_sequence_should_discard("[13;2u") && !ava::tui::terminal_escape_sequence_should_discard("[200~") &&
             !ava::tui::terminal_escape_sequence_should_discard("[13;2"),
         "terminal escape parser discards completed terminal controls while preserving AVA-owned key and paste markers");
}
void run_tui_terminal_input_tests_part_2()
{
  std::string utf8_input = std::string("ab") + "\xC3\xA9";
  ava::tui::erase_last_utf8_codepoint(utf8_input);
  expect(utf8_input == "ab", "tui backspace erases a complete utf-8 codepoint");
  std::string orphan_continuation = std::string("a") + std::string("\x80", 1);
  ava::tui::erase_last_utf8_codepoint(orphan_continuation);
  expect(orphan_continuation == "a", "tui backspace erases only a trailing orphan utf-8 continuation byte");
  std::string orphan_after_utf8 = std::string("a") + "\xC3\xA9" + std::string("\x80", 1);
  ava::tui::erase_last_utf8_codepoint(orphan_after_utf8);
  expect(orphan_after_utf8 == std::string("a") + "\xC3\xA9", "tui backspace preserves the preceding utf-8 codepoint when erasing an orphan continuation byte");
  std::string incomplete_starter = std::string("a") + std::string("\xE2", 1);
  ava::tui::erase_last_utf8_codepoint(incomplete_starter);
  expect(incomplete_starter == "a", "tui backspace erases an incomplete trailing utf-8 starter byte");
  std::string incomplete_starter_with_continuation = std::string("a") + std::string("\xF0\x9F", 2);
  ava::tui::erase_last_utf8_codepoint(incomplete_starter_with_continuation);
  expect(incomplete_starter_with_continuation == std::string("a") + std::string("\xF0", 1),
         "tui backspace erases only one byte from an incomplete trailing utf-8 sequence");

  ava::tui::ComposerDraftState draft;
  expect(ava::tui::insert_composer_draft_text(draft, std::string("a") + "\xC3\xA9") && draft.text == std::string("a") + "\xC3\xA9" &&
             draft.cursor == draft.text.size(),
         "tui draft editor inserts utf-8 text at the cursor");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteBackward) && draft.text == "a",
         "tui draft editor deletes a complete utf-8 codepoint before the cursor");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == std::string("a") + "\xC3\xA9",
         "tui draft editor undo restores the previous edit");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Redo) && draft.text == "a", "tui draft editor redo reapplies the undone edit");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == std::string("a") + "\xC3\xA9",
         "tui draft editor can undo again after redo");
  ava::tui::reset_composer_draft(draft, "alpha beta", std::string("alpha ").size());
  expect(ava::tui::replace_composer_draft_range(draft, std::string("alpha ").size(), draft.text.size(), "TWO") && draft.text == "alpha TWO" &&
             draft.cursor == std::string("alpha TWO").size(),
         "tui draft editor replaces selected byte ranges and moves the cursor after the replacement");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == "alpha beta" && draft.cursor == std::string("alpha ").size(),
         "tui draft editor range replacement participates in undo history");
  ava::tui::reset_composer_draft(draft, std::string("a") + "\xC3\xA9");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorLeft) && draft.cursor == 1,
         "tui draft editor moves left by full utf-8 codepoint boundaries");
  ava::tui::reset_composer_draft(draft, std::string("a") + "\xC3\xA9" + "b", 1);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteForward) && draft.text == "ab" && draft.cursor == 1,
         "tui draft editor forward-delete removes a full utf-8 codepoint after the cursor");
  ava::tui::reset_composer_draft(draft, std::string("a") + "\xC3\xA9", 1);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteBackward) && draft.text == std::string("\xC3\xA9"),
         "tui draft editor preserves the utf-8 codepoint after deleting preceding ascii text");
  ava::tui::reset_composer_draft(draft, "one two three");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 8,
         "tui draft editor moves to the previous word start");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteWordBackward) && draft.text == "one three" && draft.kill_buffer == "two ",
         "tui draft editor deletes the previous word into the kill buffer");
  ava::tui::reset_composer_draft(draft, "one two three", 3);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteWordForward) && draft.text == "one three" && draft.cursor == 3 &&
             draft.kill_buffer == " two",
         "tui draft editor deletes the next word into the kill buffer");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == "one two three" && draft.cursor == 3,
         "tui draft editor undo restores forward word deletion");
  ava::tui::reset_composer_draft(draft, "path/to/file", 0);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == 4 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == 5 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == 7 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == 8 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == draft.text.size(),
         "tui draft editor word-right stops at path punctuation boundaries");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 8 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 7 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 5 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 4 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 0,
         "tui draft editor word-left stops at path punctuation boundaries");
  ava::tui::reset_composer_draft(draft, "foo...bar", 0);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == 3 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == 6 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == draft.text.size(),
         "tui draft editor word-right treats punctuation runs as one segment");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 6 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 3 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 0,
         "tui draft editor word-left treats punctuation runs as one segment");
  ava::tui::reset_composer_draft(draft, "path/to/file", 7);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteWordBackward) && draft.text == "path//file" && draft.cursor == 5 &&
             draft.kill_buffer == "to",
         "tui draft editor word-backspace deletes only the preceding path segment");
  ava::tui::reset_composer_draft(draft, "path/to/file", 4);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteWordForward) && draft.text == "pathto/file" && draft.cursor == 4 &&
             draft.kill_buffer == "/",
         "tui draft editor forward word deletion can delete a punctuation segment independently");
  auto const cjk_hello = std::string("\xE4\xBD\xA0\xE5\xA5\xBD");
  auto const fullwidth_comma = std::string("\xEF\xBC\x8C");
  auto const cjk_world = std::string("\xE4\xB8\x96\xE7\x95\x8C");
  auto const cjk_sentence = cjk_hello + fullwidth_comma + cjk_world;
  auto const cjk_after_hello = cjk_hello.size();
  auto const cjk_after_comma = cjk_after_hello + fullwidth_comma.size();
  ava::tui::reset_composer_draft(draft, cjk_sentence);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == cjk_after_comma,
         "tui draft editor word-left stops after fullwidth punctuation before a CJK word");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == cjk_after_hello,
         "tui draft editor word-left treats fullwidth punctuation as its own segment");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 0,
         "tui draft editor word-left moves over the preceding CJK word");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == cjk_after_hello,
         "tui draft editor word-right moves over the first CJK word");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == cjk_after_comma,
         "tui draft editor word-right stops after fullwidth punctuation");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == cjk_sentence.size(),
         "tui draft editor word-right moves over the final CJK word");
  auto const mixed_cjk = std::string("hello") + cjk_hello + fullwidth_comma + "world" + cjk_world;
  auto const mixed_after_ascii_hello = std::string("hello").size();
  auto const mixed_after_cjk_hello = mixed_after_ascii_hello + cjk_hello.size();
  auto const mixed_after_comma = mixed_after_cjk_hello + fullwidth_comma.size();
  auto const mixed_after_ascii_world = mixed_after_comma + std::string("world").size();
  ava::tui::reset_composer_draft(draft, mixed_cjk);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == mixed_after_ascii_world,
         "tui draft editor word-left separates adjacent ASCII and CJK words");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == mixed_after_comma,
         "tui draft editor word-left moves over ASCII after fullwidth punctuation");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == mixed_after_cjk_hello,
         "tui draft editor word-left stops on fullwidth punctuation in mixed text");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == mixed_after_ascii_hello,
         "tui draft editor word-left moves over CJK before ASCII");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordLeft) && draft.cursor == 0,
         "tui draft editor word-left moves over leading ASCII before CJK");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == mixed_after_ascii_hello &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == mixed_after_cjk_hello &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == mixed_after_comma &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == mixed_after_ascii_world &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorWordRight) && draft.cursor == mixed_cjk.size(),
         "tui draft editor word-right steps through mixed ASCII, CJK, and fullwidth punctuation segments");
  ava::tui::reset_composer_draft(draft, cjk_sentence, cjk_after_comma);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteWordBackward) && draft.text == cjk_hello + cjk_world &&
             draft.cursor == cjk_after_hello && draft.kill_buffer == fullwidth_comma,
         "tui draft editor word-backspace deletes fullwidth punctuation as its own segment");
  ava::tui::reset_composer_draft(draft, "first\nsecond line");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorLineStart) && draft.cursor == 6,
         "tui draft editor moves to the start of the current line");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorLineEnd) && draft.cursor == draft.text.size(),
         "tui draft editor moves to the end of the current line");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteToLineStart) && draft.text == "first\n" && draft.kill_buffer == "second line",
         "tui draft editor deletes from cursor to line start into the kill buffer");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Yank) && draft.text == "first\nsecond line",
         "tui draft editor yanks the last killed line text");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == "first\n", "tui draft editor undo removes the yanked text");
  ava::tui::reset_composer_draft(draft, "first\nsecond line", 6);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteToLineEnd) && draft.text == "first\n" && draft.kill_buffer == "second line",
         "tui draft editor deletes from cursor to line end into the kill buffer");
  ava::tui::reset_composer_draft(draft, "join\nline", 4);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteToLineEnd) && draft.text == "joinline" && draft.cursor == 4 &&
             draft.kill_buffer == "\n",
         "tui draft editor joins the next line when deleting at line end");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteToLineEnd) && draft.text == "join" && draft.cursor == 4 &&
             draft.kill_buffer == "\nline",
         "tui draft editor accumulates consecutive forward kills into one kill-ring entry");
  ava::tui::reset_composer_draft(draft, "last", 4);
  expect(!ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::DeleteToLineEnd) && draft.text == "last" && draft.cursor == 4,
         "tui draft editor leaves final line end unchanged when there is nothing to delete");
  ava::tui::reset_composer_draft(draft, "abc\ndef\nghij", 6);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorUp) && draft.cursor == 2,
         "tui draft editor moves vertically to the previous line at the same column");
  expect(!ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorUp) && draft.cursor == 2,
         "tui draft editor leaves cursor unchanged when vertical movement is already at the first line");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorDown) && draft.cursor == 6 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorDown) && draft.cursor == 10,
         "tui draft editor moves vertically to following lines at the same column");
  ava::tui::reset_composer_draft(draft, "abcdef\nx\nabcdef", 5);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorDown) && draft.cursor == 8 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorDown) && draft.cursor == 14,
         "tui draft editor preserves sticky column across shorter lines");
  ava::tui::reset_composer_draft(draft, "abcdef\nx\nabcdef", 5);
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorDown) && draft.cursor == 8 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorLeft) && draft.cursor == 7 &&
             ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::CursorDown) && draft.cursor == 9,
         "tui draft editor resets sticky column after horizontal movement");
  ava::tui::reset_composer_draft(draft, "hello world", 0);
  expect(ava::tui::jump_composer_draft_to_character(draft, "o", true) && draft.cursor == 4,
         "tui draft editor jumps forward to the first matching character after the cursor");
  expect(ava::tui::jump_composer_draft_to_character(draft, "o", true) && draft.cursor == 7,
         "tui draft editor jumps forward to the next matching character after the cursor");
  expect(!ava::tui::jump_composer_draft_to_character(draft, "z", true) && draft.cursor == 7,
         "tui draft editor leaves the cursor unchanged when a forward jump target is missing");
  expect(ava::tui::jump_composer_draft_to_character(draft, "h", false) && draft.cursor == 0, "tui draft editor jumps backward across the current draft");
  ava::tui::reset_composer_draft(draft, "abc\ndef\nghi", 0);
  expect(ava::tui::jump_composer_draft_to_character(draft, "g", true) && draft.cursor == 8, "tui draft editor jumps forward across lines");
  expect(ava::tui::jump_composer_draft_to_character(draft, "d", false) && draft.cursor == 4, "tui draft editor jumps backward across lines");
  ava::tui::reset_composer_draft(draft, "Hello World", 0);
  expect(!ava::tui::jump_composer_draft_to_character(draft, "h", true) && draft.cursor == 0 && ava::tui::jump_composer_draft_to_character(draft, "W", true) &&
             draft.cursor == 6,
         "tui draft editor character jumps are case-sensitive");
  ava::tui::reset_composer_draft(draft, "pending message");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::ClearInput) && draft.text.empty() && draft.cursor == 0 &&
             draft.kill_buffer == "pending message",
         "tui draft editor clears a non-empty composer draft into the kill buffer");
  expect(ava::tui::apply_composer_draft_action(draft, ava::tui::TuiAction::Undo) && draft.text == "pending message",
         "tui draft editor undo restores a cleared composer draft");
  ava::tui::ComposerDraftState backslash_newline_draft;
  expect(ava::tui::insert_composer_draft_text(backslash_newline_draft, "\\") && backslash_newline_draft.text == "\\",
         "tui draft editor inserts backslash visibly before Enter workaround");
  expect(ava::tui::replace_composer_backslash_before_cursor_with_newline(backslash_newline_draft) && backslash_newline_draft.text == "\n" &&
             backslash_newline_draft.cursor == 1,
         "tui draft editor converts a standalone backslash before cursor into a newline");
  expect(ava::tui::apply_composer_draft_action(backslash_newline_draft, ava::tui::TuiAction::Undo) && backslash_newline_draft.text == "\\",
         "tui draft editor can undo the backslash-enter newline workaround");
  ava::tui::reset_composer_draft(backslash_newline_draft, "\\\\\\");
  expect(ava::tui::replace_composer_backslash_before_cursor_with_newline(backslash_newline_draft) && backslash_newline_draft.text == "\\\\\n",
         "tui draft editor removes only the backslash immediately before cursor for Enter newline workaround");
  ava::tui::reset_composer_draft(backslash_newline_draft, "\\x");
  expect(!ava::tui::replace_composer_backslash_before_cursor_with_newline(backslash_newline_draft) && backslash_newline_draft.text == "\\x",
         "tui draft editor leaves ordinary backslash text alone when backslash is not before cursor");
  ava::tui::reset_composer_draft(backslash_newline_draft, "\\x", 1);
  expect(ava::tui::replace_composer_backslash_before_cursor_with_newline(backslash_newline_draft) && backslash_newline_draft.text == "\nx",
         "tui draft editor applies the backslash-enter newline workaround at the current cursor");
  ava::tui::ComposerDraftState ring_draft;
  ava::tui::reset_composer_draft(ring_draft, "alpha beta gamma");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::DeleteWordBackward) && ring_draft.text == "alpha beta " &&
             ring_draft.kill_buffer == "gamma",
         "tui draft editor records killed text in a ring");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::DeleteWordBackward) && ring_draft.text == "alpha " &&
             ring_draft.kill_buffer == "beta gamma" && ring_draft.kill_ring.size() == 1 && ring_draft.kill_ring.front() == "beta gamma",
         "tui draft editor prepends consecutive backward kills into one kill-ring entry");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::Yank) && ring_draft.text == "alpha beta gamma",
         "tui draft editor yanks the accumulated kill-ring entry");
  ava::tui::reset_composer_draft(ring_draft, "one two three");
  ring_draft.kill_ring.clear();
  ring_draft.kill_buffer.clear();
  ring_draft.kill_sequence = ava::tui::ComposerKillSequence::None;
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::DeleteWordBackward) && ring_draft.kill_buffer == "three" &&
             ring_draft.kill_ring.size() == 1,
         "tui draft editor seeds a fresh kill-ring entry after reset");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::CursorLeft) &&
             ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::DeleteWordBackward) && ring_draft.kill_buffer == "two" &&
             ring_draft.kill_ring.size() == 2 && ring_draft.kill_ring.front() == "two" && ring_draft.kill_ring[1] == "three",
         "tui draft editor breaks kill accumulation after cursor movement");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::Yank) && ring_draft.kill_buffer == "two",
         "tui draft editor yanks the newest kill-ring entry after a broken sequence");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::YankPop) && ring_draft.kill_buffer == "three",
         "tui draft editor yank-pop swaps the previous yank with the next kill-ring entry");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::YankPop) && ring_draft.kill_buffer == "two",
         "tui draft editor yank-pop cycles through the kill ring");

  std::vector<std::string> input_history;
  expect(!ava::tui::push_composer_input_history(input_history, "   \t  "), "tui input history ignores empty and whitespace-only submissions");
  expect(ava::tui::push_composer_input_history(input_history, " first prompt ") && input_history.back() == "first prompt",
         "tui input history trims submitted prompts before storing them");
  expect(!ava::tui::push_composer_input_history(input_history, "first prompt"), "tui input history skips consecutive duplicates");
  expect(ava::tui::push_composer_input_history(input_history, "second prompt") && ava::tui::push_composer_input_history(input_history, "first prompt"),
         "tui input history keeps non-consecutive duplicates");

  ava::tui::ComposerDraftState history_draft;
  ava::tui::reset_composer_draft(history_draft, "draft text", 5);
  std::optional<std::size_t> history_index;
  std::string draft_input;
  expect(ava::tui::browse_composer_input_history(history_draft, input_history, history_index, draft_input, true) && history_draft.text == "first prompt" &&
             history_draft.cursor == 0 && history_index && *history_index == 2,
         "tui input history recalls the newest item with the cursor at the start on previous");
  expect(ava::tui::browse_composer_input_history(history_draft, input_history, history_index, draft_input, true) && history_draft.text == "second prompt" &&
             history_draft.cursor == 0 && history_index && *history_index == 1,
         "tui input history walks backward through older items");
  expect(ava::tui::browse_composer_input_history(history_draft, input_history, history_index, draft_input, false) && history_draft.text == "first prompt" &&
             history_draft.cursor == std::string("first prompt").size() && history_index && *history_index == 2,
         "tui input history walks forward toward newer items with the cursor at the end");
  expect(ava::tui::browse_composer_input_history(history_draft, input_history, history_index, draft_input, false) && history_draft.text == "draft text" &&
             history_draft.cursor == std::string("draft text").size() && !history_index,
         "tui input history restores the in-progress draft after the newest item");
  expect(!ava::tui::browse_composer_input_history(history_draft, input_history, history_index, draft_input, false),
         "tui input history next does nothing when not browsing");
  static_cast<void>(ava::tui::browse_composer_input_history(history_draft, input_history, history_index, draft_input, true));
  ava::tui::clear_composer_input_history_browse(history_index, draft_input);
  expect(!history_index && draft_input.empty(), "tui input history browsing state clears when the draft is edited");

  std::vector<std::string> capped_history;
  for (int index = 0; index < 105; ++index) static_cast<void>(ava::tui::push_composer_input_history(capped_history, "item " + std::to_string(index)));
  expect(capped_history.size() == 100 && capped_history.front() == "item 5" && capped_history.back() == "item 104",
         "tui input history keeps the newest 100 entries");

  expect(ava::tui::normalize_composer_paste_text("one\r\ntwo\rthree") == "one\ntwo\nthree",
         "tui paste normalizes crlf and lone carriage returns into newlines");
  expect(ava::tui::normalize_composer_paste_text(std::string("a\x01\tb\n", 5) + "\xC3\xA9") == std::string("a\tb\n", 4) + "\xC3\xA9",
         "tui paste strips controls while preserving tabs, newlines, and utf-8 bytes");
  ava::tui::ComposerDraftState paste_draft;
  expect(ava::tui::insert_composer_paste_text(paste_draft, "one\ntwo") && paste_draft.text == "one\ntwo" &&
             ava::tui::expanded_composer_draft_text(paste_draft) == "one\ntwo",
         "tui small bracketed paste remains literal in the draft and submitted text");
  ava::tui::reset_composer_draft(paste_draft);
  auto const large_paste = std::string("line01\nline02\nline03\nline04\nline05\nline06\nline07\nline08\nline09\nline10\nline11");
  expect(ava::tui::insert_composer_paste_text(paste_draft, large_paste) && paste_draft.text == "[paste #1 +11 lines]" &&
             paste_draft.cursor == paste_draft.text.size() && ava::tui::expanded_composer_draft_text(paste_draft) == large_paste,
         "tui large bracketed paste collapses to a marker while preserving exact submitted text");
  auto make_marker_draft = [&]() {
    ava::tui::ComposerDraftState marker_draft;
    static_cast<void>(ava::tui::insert_composer_draft_text(marker_draft, "A"));
    static_cast<void>(ava::tui::insert_composer_paste_text(marker_draft, large_paste));
    static_cast<void>(ava::tui::insert_composer_draft_text(marker_draft, "B"));
    return marker_draft;
  };
  auto marker_draft = make_marker_draft();
  auto const marker_size = marker_draft.text.size() - 2;
  marker_draft.cursor = 0;
  expect(ava::tui::apply_composer_draft_action(marker_draft, ava::tui::TuiAction::CursorRight) && marker_draft.cursor == 1 &&
             ava::tui::apply_composer_draft_action(marker_draft, ava::tui::TuiAction::CursorRight) && marker_draft.cursor == 1 + marker_size &&
             ava::tui::apply_composer_draft_action(marker_draft, ava::tui::TuiAction::CursorRight) && marker_draft.cursor == marker_draft.text.size(),
         "tui draft editor treats recorded paste markers as one unit for right-arrow movement");
  expect(ava::tui::apply_composer_draft_action(marker_draft, ava::tui::TuiAction::CursorLeft) && marker_draft.cursor == 1 + marker_size &&
             ava::tui::apply_composer_draft_action(marker_draft, ava::tui::TuiAction::CursorLeft) && marker_draft.cursor == 1,
         "tui draft editor treats recorded paste markers as one unit for left-arrow movement");
  auto const marker_start = std::size_t{1};
  auto const marker_end = marker_start + marker_size;
  expect(ava::tui::clamp_composer_draft_cursor_to_atomic_boundary(marker_draft, marker_start + 1) == marker_start &&
             ava::tui::clamp_composer_draft_cursor_to_atomic_boundary(marker_draft, marker_end - 1) == marker_end &&
             ava::tui::clamp_composer_draft_cursor_to_atomic_boundary(marker_draft, marker_start) == marker_start &&
             ava::tui::clamp_composer_draft_cursor_to_atomic_boundary(marker_draft, marker_end) == marker_end,
         "tui draft editor snaps cursor positions inside recorded paste markers to atomic boundaries");
  auto word_marker_draft = [&]() {
    ava::tui::ComposerDraftState marker_word_draft;
    static_cast<void>(ava::tui::insert_composer_draft_text(marker_word_draft, "X "));
    static_cast<void>(ava::tui::insert_composer_paste_text(marker_word_draft, large_paste));
    static_cast<void>(ava::tui::insert_composer_draft_text(marker_word_draft, " Y"));
    return marker_word_draft;
  }();
  auto const word_marker_size = word_marker_draft.text.size() - 4;
  word_marker_draft.cursor = 0;
  expect(ava::tui::apply_composer_draft_action(word_marker_draft, ava::tui::TuiAction::CursorWordRight) && word_marker_draft.cursor == 1 &&
             ava::tui::apply_composer_draft_action(word_marker_draft, ava::tui::TuiAction::CursorWordRight) &&
             word_marker_draft.cursor == 2 + word_marker_size &&
             ava::tui::apply_composer_draft_action(word_marker_draft, ava::tui::TuiAction::CursorWordRight) &&
             word_marker_draft.cursor == word_marker_draft.text.size(),
         "tui draft editor treats recorded paste markers as one unit for word-right movement");
  expect(ava::tui::apply_composer_draft_action(word_marker_draft, ava::tui::TuiAction::CursorWordLeft) && word_marker_draft.cursor == 3 + word_marker_size &&
             ava::tui::apply_composer_draft_action(word_marker_draft, ava::tui::TuiAction::CursorWordLeft) && word_marker_draft.cursor == 2,
         "tui draft editor treats recorded paste markers as one unit for word-left movement");
  word_marker_draft.cursor = 2 + word_marker_size;
  expect(ava::tui::apply_composer_draft_action(word_marker_draft, ava::tui::TuiAction::DeleteWordBackward) && word_marker_draft.text == "X  Y" &&
             word_marker_draft.cursor == 2 && ava::tui::expanded_composer_draft_text(word_marker_draft) == "X  Y",
         "tui draft editor deletes a recorded paste marker as one unit with word-backspace");
  auto forward_word_marker_draft = [&]() {
    ava::tui::ComposerDraftState marker_word_draft;
    static_cast<void>(ava::tui::insert_composer_draft_text(marker_word_draft, "X "));
    static_cast<void>(ava::tui::insert_composer_paste_text(marker_word_draft, large_paste));
    static_cast<void>(ava::tui::insert_composer_draft_text(marker_word_draft, " Y"));
    return marker_word_draft;
  }();
  forward_word_marker_draft.cursor = 2;
  expect(ava::tui::apply_composer_draft_action(forward_word_marker_draft, ava::tui::TuiAction::DeleteWordForward) && forward_word_marker_draft.text == "X  Y" &&
             forward_word_marker_draft.cursor == 2 && ava::tui::expanded_composer_draft_text(forward_word_marker_draft) == "X  Y",
         "tui draft editor deletes a recorded paste marker as one unit with forward word deletion");
  auto delete_marker_draft = make_marker_draft();
  delete_marker_draft.cursor = delete_marker_draft.text.size() - 1;
  expect(ava::tui::apply_composer_draft_action(delete_marker_draft, ava::tui::TuiAction::DeleteBackward) && delete_marker_draft.text == "AB" &&
             delete_marker_draft.cursor == 1 && ava::tui::expanded_composer_draft_text(delete_marker_draft) == "AB",
         "tui draft editor deletes a recorded paste marker as one unit with backspace");
  auto forward_delete_marker_draft = make_marker_draft();
  forward_delete_marker_draft.cursor = 1;
  expect(ava::tui::apply_composer_draft_action(forward_delete_marker_draft, ava::tui::TuiAction::DeleteForward) && forward_delete_marker_draft.text == "AB" &&
             forward_delete_marker_draft.cursor == 1 && ava::tui::expanded_composer_draft_text(forward_delete_marker_draft) == "AB",
         "tui draft editor deletes a recorded paste marker as one unit with forward delete");
  expect(ava::tui::apply_composer_draft_action(paste_draft, ava::tui::TuiAction::Undo) && paste_draft.text.empty() &&
             ava::tui::apply_composer_draft_action(paste_draft, ava::tui::TuiAction::Redo) &&
             ava::tui::expanded_composer_draft_text(paste_draft) == large_paste,
         "tui large paste markers survive undo and redo expansion");
  ava::tui::reset_composer_draft(paste_draft, "[paste #1 +11 lines]");
  expect(ava::tui::expanded_composer_draft_text(paste_draft) == "[paste #1 +11 lines]",
         "tui manually typed paste-marker text is not expanded without a recorded paste entry");
  expect(ava::tui::apply_composer_draft_action(paste_draft, ava::tui::TuiAction::CursorLineStart) &&
             ava::tui::apply_composer_draft_action(paste_draft, ava::tui::TuiAction::CursorRight) && paste_draft.cursor == 1,
         "tui manually typed paste-marker text is not treated as an atomic marker");
  expect(ava::tui::clamp_composer_draft_cursor_to_atomic_boundary(paste_draft, 3) == 3,
         "tui manually typed paste-marker text does not receive recorded-marker cursor snapping");
  auto duplicate_marker_draft = make_marker_draft();
  auto const duplicate_marker_text = duplicate_marker_draft.text.substr(1, marker_size);
  duplicate_marker_draft.cursor = duplicate_marker_draft.text.size();
  static_cast<void>(ava::tui::insert_composer_draft_text(duplicate_marker_draft, duplicate_marker_text));
  auto const duplicate_marker_start = duplicate_marker_draft.text.size() - duplicate_marker_text.size();
  duplicate_marker_draft.cursor = duplicate_marker_start;
  expect(ava::tui::apply_composer_draft_action(duplicate_marker_draft, ava::tui::TuiAction::CursorRight) &&
             duplicate_marker_draft.cursor == duplicate_marker_start + 1 &&
             ava::tui::expanded_composer_draft_text(duplicate_marker_draft) == std::string("A") + large_paste + "B" + duplicate_marker_text,
         "tui duplicate manually typed paste-marker text remains literal beside a recorded marker");
  duplicate_marker_draft.cursor = duplicate_marker_start;
  expect(ava::tui::apply_composer_draft_action(duplicate_marker_draft, ava::tui::TuiAction::CursorWordRight) &&
             duplicate_marker_draft.cursor == duplicate_marker_start + 1,
         "tui duplicate manually typed paste-marker text is punctuation-aware but not atomic for word movement");
  ava::tui::reset_composer_draft(paste_draft);
  auto const long_single_line = std::string(2001, 'x');
  expect(ava::tui::insert_composer_paste_text(paste_draft, long_single_line) && paste_draft.text == "[paste #1 2001 chars]" &&
             ava::tui::expanded_composer_draft_text(paste_draft) == long_single_line,
         "tui large single-line paste collapses by byte count and expands for submit");

  // Wave A editor fidelity: cluster-aware editing, typing undo groups, kill accumulation.
  auto const combining_acute = std::string("\xCC\x81");
  auto const e_combining = std::string("e") + combining_acute;
  auto const regional_c = std::string("\xF0\x9F\x87\xA8");
  auto const regional_n = std::string("\xF0\x9F\x87\xB3");
  auto const flag_cn = regional_c + regional_n;
  auto const thumbs_up = std::string("\xF0\x9F\x91\x8D");
  auto const light_skin_tone = std::string("\xF0\x9F\x8F\xBB");
  auto const skin_tone_thumbs = thumbs_up + light_skin_tone;
  auto const man = std::string("\xF0\x9F\x91\xA8");
  auto const woman = std::string("\xF0\x9F\x91\xA9");
  auto const girl = std::string("\xF0\x9F\x91\xA7");
  auto const zwj = std::string("\xE2\x80\x8D");
  auto const family_zwj = man + zwj + woman + zwj + girl;
  auto const malformed_bytes = std::string("a") + std::string("\x80\xFF", 2) + "b";

  expect(ava::tui::detail::terminal_text_cluster_bytes(e_combining, 0) == e_combining.size() &&
             ava::tui::detail::terminal_text_cluster_bytes(flag_cn, 0) == flag_cn.size() &&
             ava::tui::detail::terminal_text_cluster_bytes(skin_tone_thumbs, 0) == skin_tone_thumbs.size() &&
             ava::tui::detail::terminal_text_cluster_bytes(family_zwj, 0) == family_zwj.size() &&
             ava::tui::detail::terminal_text_cluster_bytes(malformed_bytes, 1) == 1,
         "tui shared cluster helper covers combining marks, flags, skin tones, ZWJ families, and malformed bytes");

  ava::tui::ComposerDraftState cluster_draft;
  ava::tui::reset_composer_draft(cluster_draft, std::string("x") + e_combining + "y");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 1 + e_combining.size() &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 1 &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 0,
         "tui draft editor moves left over base+combining clusters atomically");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorRight) && cluster_draft.cursor == 1 &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorRight) && cluster_draft.cursor == 1 + e_combining.size() &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorRight) && cluster_draft.cursor == cluster_draft.text.size(),
         "tui draft editor moves right over base+combining clusters atomically");
  ava::tui::reset_composer_draft(cluster_draft, std::string("x") + e_combining + "y");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteBackward) && cluster_draft.text == std::string("x") + e_combining &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteBackward) && cluster_draft.text == "x" &&
             cluster_draft.kill_buffer.empty(),
         "tui draft editor backspaces base+combining clusters without updating the kill ring");
  ava::tui::reset_composer_draft(cluster_draft, std::string("x") + e_combining + "y", 1);
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteForward) && cluster_draft.text == "xy" && cluster_draft.cursor == 1,
         "tui draft editor forward-deletes base+combining clusters atomically");

  ava::tui::reset_composer_draft(cluster_draft, std::string("a") + flag_cn + "b");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 1 + flag_cn.size() &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 1 &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteForward) && cluster_draft.text == "ab",
         "tui draft editor treats regional-indicator flag pairs as one atomic cluster");

  ava::tui::reset_composer_draft(cluster_draft, std::string("a") + skin_tone_thumbs + "b");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 1 + skin_tone_thumbs.size() &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteBackward) && cluster_draft.text == "ab",
         "tui draft editor treats emoji skin-tone modifier sequences as one atomic cluster");

  ava::tui::reset_composer_draft(cluster_draft, std::string("a") + family_zwj + "b");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 1 + family_zwj.size() &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteBackward) && cluster_draft.text == "ab",
         "tui draft editor treats family ZWJ emoji sequences as one atomic cluster");

  ava::tui::reset_composer_draft(cluster_draft, malformed_bytes);
  expect(cluster_draft.text.size() == 4 && cluster_draft.cursor == 4, "tui draft editor loads malformed utf-8 draft at end");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteBackward) &&
             cluster_draft.text == std::string("a") + std::string("\x80\xFF", 2) && cluster_draft.cursor == 3,
         "tui draft editor backspaces trailing ascii after malformed utf-8");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteBackward) &&
             cluster_draft.text == std::string("a") + std::string("\x80", 1) && cluster_draft.cursor == 2,
         "tui draft editor backspaces the 0xFF malformed byte");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteBackward) && cluster_draft.text == "a" && cluster_draft.cursor == 1,
         "tui draft editor backspaces the 0x80 malformed byte");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 0 &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteForward) && cluster_draft.text.empty(),
         "tui draft editor forward-deletes the remaining ascii after malformed cleanup");
  ava::tui::reset_composer_draft(cluster_draft, malformed_bytes, 1);
  expect(cluster_draft.cursor == 1, "tui draft editor keeps the cursor on orphan utf-8 continuation bytes");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::DeleteForward) &&
             cluster_draft.text == std::string("a") + std::string("\xFF", 1) + "b" && cluster_draft.cursor == 1,
         "tui draft editor forward-deletes malformed utf-8 one byte at a time");
  expect(ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorLeft) && cluster_draft.cursor == 0 &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorRight) && cluster_draft.cursor == 1 &&
             ava::tui::apply_composer_draft_action(cluster_draft, ava::tui::TuiAction::CursorRight) && cluster_draft.cursor == 2,
         "tui draft editor moves across malformed utf-8 one byte at a time");

  auto const large_paste_for_atomic = std::string("line01\nline02\nline03\nline04\nline05\nline06\nline07\nline08\nline09\nline10\nline11");
  ava::tui::ComposerDraftState paste_atomic_draft;
  expect(ava::tui::insert_composer_draft_text(paste_atomic_draft, "A") && ava::tui::insert_composer_paste_text(paste_atomic_draft, large_paste_for_atomic) &&
             ava::tui::insert_composer_draft_text(paste_atomic_draft, "B"),
         "tui draft editor builds a paste-marker draft for atomicity checks");
  auto const paste_marker_size = paste_atomic_draft.text.size() - 2;
  paste_atomic_draft.cursor = paste_atomic_draft.text.size();
  expect(ava::tui::apply_composer_draft_action(paste_atomic_draft, ava::tui::TuiAction::DeleteBackward) &&
             paste_atomic_draft.text.size() == 1 + paste_marker_size &&
             ava::tui::apply_composer_draft_action(paste_atomic_draft, ava::tui::TuiAction::DeleteBackward) && paste_atomic_draft.text == "A",
         "tui draft editor deletes recorded paste markers as one atomic unit");

  ava::tui::ComposerDraftState undo_group_draft;
  expect(ava::tui::insert_composer_draft_text(undo_group_draft, "h") && ava::tui::insert_composer_draft_text(undo_group_draft, "i") &&
             ava::tui::insert_composer_draft_text(undo_group_draft, "!") && undo_group_draft.text == "hi!" && undo_group_draft.undo_stack.size() == 1,
         "tui draft editor coalesces contiguous ordinary typing into one undo group");
  expect(ava::tui::insert_composer_draft_text(undo_group_draft, " ") && undo_group_draft.text == "hi! " && undo_group_draft.undo_stack.size() == 2,
         "tui draft editor breaks typing undo groups at whitespace");
  expect(ava::tui::insert_composer_draft_text(undo_group_draft, "y") && ava::tui::insert_composer_draft_text(undo_group_draft, "o") &&
             undo_group_draft.text == "hi! yo" && undo_group_draft.undo_stack.size() == 3,
         "tui draft editor starts a new coalesced word-run undo group after whitespace");
  expect(ava::tui::apply_composer_draft_action(undo_group_draft, ava::tui::TuiAction::Undo) && undo_group_draft.text == "hi! " &&
             ava::tui::apply_composer_draft_action(undo_group_draft, ava::tui::TuiAction::Undo) && undo_group_draft.text == "hi!" &&
             ava::tui::apply_composer_draft_action(undo_group_draft, ava::tui::TuiAction::Undo) && undo_group_draft.text.empty(),
         "tui draft editor undoes coalesced word runs and whitespace boundaries as separate groups");
  expect(ava::tui::apply_composer_draft_action(undo_group_draft, ava::tui::TuiAction::Redo) && undo_group_draft.text == "hi!" &&
             ava::tui::apply_composer_draft_action(undo_group_draft, ava::tui::TuiAction::Redo) && undo_group_draft.text == "hi! " &&
             ava::tui::apply_composer_draft_action(undo_group_draft, ava::tui::TuiAction::Redo) && undo_group_draft.text == "hi! yo",
         "tui draft editor redo reapplies coalesced typing groups exactly");

  ava::tui::ComposerDraftState newline_undo_draft;
  expect(ava::tui::insert_composer_draft_text(newline_undo_draft, "ab") && ava::tui::insert_composer_draft_text(newline_undo_draft, "\n") &&
             ava::tui::insert_composer_draft_text(newline_undo_draft, "cd") && newline_undo_draft.undo_stack.size() == 3,
         "tui draft editor breaks typing undo groups at newlines");

  ava::tui::ComposerDraftState cursor_break_draft;
  expect(ava::tui::insert_composer_draft_text(cursor_break_draft, "ab") && cursor_break_draft.undo_stack.size() == 1,
         "tui draft editor records one undo group for an initial word run");
  expect(ava::tui::apply_composer_draft_action(cursor_break_draft, ava::tui::TuiAction::CursorLeft) &&
             ava::tui::insert_composer_draft_text(cursor_break_draft, "X") && cursor_break_draft.text == "aXb" && cursor_break_draft.undo_stack.size() == 2,
         "tui draft editor breaks typing undo groups after cursor movement");
  expect(ava::tui::apply_composer_draft_action(cursor_break_draft, ava::tui::TuiAction::Undo) && cursor_break_draft.text == "ab" &&
             ava::tui::apply_composer_draft_action(cursor_break_draft, ava::tui::TuiAction::Undo) && cursor_break_draft.text.empty(),
         "tui draft editor undoes post-cursor-break inserts separately from the earlier word run");

  ava::tui::ComposerDraftState undo_cap_draft;
  for (int index = 0; index < 120; ++index)
  {
    static_cast<void>(ava::tui::insert_composer_draft_text(undo_cap_draft, "x"));
    static_cast<void>(ava::tui::insert_composer_draft_text(undo_cap_draft, " "));
  }
  expect(undo_cap_draft.undo_stack.size() == 100, "tui draft editor caps undo history at 100 snapshots");

  ava::tui::ComposerDraftState forward_kill_draft;
  ava::tui::reset_composer_draft(forward_kill_draft, "alpha beta gamma", 0);
  expect(ava::tui::apply_composer_draft_action(forward_kill_draft, ava::tui::TuiAction::DeleteWordForward) && forward_kill_draft.kill_buffer == "alpha" &&
             ava::tui::apply_composer_draft_action(forward_kill_draft, ava::tui::TuiAction::DeleteWordForward) &&
             forward_kill_draft.kill_buffer == "alpha beta" && forward_kill_draft.kill_ring.size() == 1,
         "tui draft editor appends consecutive forward kills into one kill-ring entry");

  ava::tui::ComposerDraftState kill_cap_ring;
  for (int index = 0; index < 20; ++index)
  {
    auto piece = "w" + std::to_string(index);
    kill_cap_ring.text = piece;
    kill_cap_ring.cursor = piece.size();
    kill_cap_ring.kill_sequence = ava::tui::ComposerKillSequence::None;
    static_cast<void>(ava::tui::apply_composer_draft_action(kill_cap_ring, ava::tui::TuiAction::DeleteWordBackward));
    // Break the kill sequence so each word becomes its own ring entry.
    static_cast<void>(ava::tui::apply_composer_draft_action(kill_cap_ring, ava::tui::TuiAction::CursorLeft));
  }
  expect(kill_cap_ring.kill_ring.size() == 16 && kill_cap_ring.kill_ring.front() == "w19" && kill_cap_ring.kill_ring.back() == "w4",
         "tui draft editor caps the kill ring at 16 entries");

  auto const split_empty = ava::tui::split_lines("");
  expect(split_empty.size() == 1 && split_empty.front().empty(), "tui split keeps empty input as one line");
  auto const split_trailing = ava::tui::split_lines("a\n");
  expect(split_trailing.size() == 2 && split_trailing[0] == "a" && split_trailing[1].empty(), "tui split preserves trailing empty line");
  auto const split_crlf = ava::tui::split_lines("a\r\nb\rc");
  expect(split_crlf.size() == 3 && split_crlf[0] == "a" && split_crlf[1] == "b" && split_crlf[2] == "c",
         "tui split treats crlf and carriage-return output as line breaks");
}
namespace {
struct VirtualTerminalProfile
{
  std::string name;
  std::string term;
  std::string term_program = {};
  std::string colorterm = {};
  std::string tmux = {};
  std::string tmux_pane = {};
  std::string ssh_tty = {};
  std::string kitty_window_id = {};
  std::string wezterm_exec = {};
  bool no_color = false;
};

struct VirtualTerminalResult
{
  bool screen_created = false;
  bool size_reported = false;
  bool base_drawn = false;
  bool modal_drawn = false;
  bool cursor_restored_after_modal = false;
  bool cached_row_draw_preserves_unchanged_lower_row = false;
  bool graphic_overlay_cache_stable = false;
  bool processing_footer_updates_stable = false;
  bool processing_footer_output_is_quiet = false;
  bool processing_footer_output_is_plain = false;
  bool cursor_forced_visible_for_teardown = false;
  bool checked_builtin_dark_default_screen_bg = false;
  bool builtin_dark_stdscr_background_is_default = false;
};

std::optional<std::string> ncurses_screen_row(std::size_t row)
{
  if (row >= static_cast<std::size_t>(LINES > 0 ? LINES : 0))
    return std::nullopt;
  std::string text;
  text.reserve(static_cast<std::size_t>(COLS > 0 ? COLS : 0));
  for (int column = 0; column < COLS; ++column)
  {
    auto const cell = mvwinch(stdscr, static_cast<int>(row), column);
    if (cell == static_cast<chtype>(ERR))
      return std::nullopt;
    text.push_back(static_cast<char>(cell & A_CHARTEXT));
  }
  return text;
}

VirtualTerminalResult exercise_virtual_terminal_profile(VirtualTerminalProfile const& profile)
{
  ScopedEnvVar term_guard("TERM", profile.term);
  ScopedEnvVar term_program_guard("TERM_PROGRAM", profile.term_program);
  ScopedEnvVar colorterm_guard("COLORTERM", profile.colorterm);
  ScopedEnvVar tmux_guard("TMUX", profile.tmux);
  ScopedEnvVar tmux_pane_guard("TMUX_PANE", profile.tmux_pane);
  ScopedEnvVar ssh_tty_guard("SSH_TTY", profile.ssh_tty);
  ScopedEnvVar kitty_window_guard("KITTY_WINDOW_ID", profile.kitty_window_id);
  ScopedEnvVar wezterm_exec_guard("WEZTERM_EXECUTABLE", profile.wezterm_exec);
  ScopedEnvVar no_color_guard("NO_COLOR", profile.no_color ? "1" : "");

  VirtualTerminalResult result;
  FILE* input = std::tmpfile();
  FILE* output = std::tmpfile();
  expect(input != nullptr && output != nullptr, "ncurses smoke test can create temporary streams for " + profile.name);
  if (!input || !output)
  {
    if (input)
      static_cast<void>(std::fclose(input));
    if (output)
      static_cast<void>(std::fclose(output));
    return result;
  }

  SCREEN* screen = newterm(nullptr, output, input);
  result.screen_created = screen != nullptr;
  if (screen)
  {
    static_cast<void>(set_term(screen));
    // Match production color setup before drawing so default-background pairs resolve.
    if (has_colors())
    {
      static_cast<void>(start_color());
      static_cast<void>(use_default_colors());
    }
    int rows = 0;
    int columns = 0;
    getmaxyx(stdscr, rows, columns);
    result.size_reported = rows > 0 && columns > 0;
    expect(result.size_reported, "ncurses smoke test reports a usable virtual screen size for " + profile.name);
    static_cast<void>(resizeterm(14, 160));
    auto const ime_sensitive_input = std::string("a") + "\xE7\x95\x8C" + "e" + "\xCC\x81";
    auto const snapshot =
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_term",
                                   .input = ime_sensitive_input,
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "virtual terminal draw keeps \xE7\x95\x8C bounded"}},
                                   .width = 160,
                                   .height = 14,
                                   .input_cursor = ime_sensitive_input.size()};
    auto const canvas = ava::tui::composer_canvas_layout(snapshot);
    auto const expected_column = canvas.left + ava::tui::detail::input_cursor_column(snapshot, canvas.content_width);
    result.base_drawn = ava::tui::draw_screen(snapshot);
    // initialize_color_pairs caches statically across SCREENs; assert default screen bg once on the dark baseline.
    if (!profile.no_color && profile.name == "xterm baseline" && has_colors())
    {
      result.checked_builtin_dark_default_screen_bg = true;
      short foreground = 0;
      short background = 0;
      auto const bkgd_cell = getbkgd(stdscr);
      auto const pair = static_cast<short>(PAIR_NUMBER(bkgd_cell));
      result.builtin_dark_stdscr_background_is_default = pair_content(pair, &foreground, &background) == OK && background == -1;
    }

    auto modal_snapshot = snapshot;
    modal_snapshot.select_list = ava::tui::SelectListView{.title = "Terminal profile",
                                                          .subtitle = profile.name,
                                                          .items = {ava::tui::SelectListItemView{.value = profile.term,
                                                                                                 .label = profile.term,
                                                                                                 .description = profile.name,
                                                                                                 .group = "terminal",
                                                                                                 .detail = "virtual newterm smoke",
                                                                                                 .badge = {},
                                                                                                 .current = true,
                                                                                                 .enabled = true,
                                                                                                 .disabled_reason = {}}},
                                                          .selected_item_index = 0,
                                                          .query = {},
                                                          .placeholder = "Search profiles",
                                                          .empty_text = "No profiles",
                                                          .footer_hint = "Esc closes"};
    result.modal_drawn = ava::tui::draw_screen(modal_snapshot);

    auto const restored_drawn = ava::tui::draw_screen(snapshot);
    int cursor_y = 0;
    int cursor_x = 0;
    getyx(stdscr, cursor_y, cursor_x);
    result.cursor_restored_after_modal = restored_drawn && cursor_x == static_cast<int>(expected_column - 1) && cursor_y >= 0 && cursor_y < LINES;

    auto exercise_cached_row_draw = [&]() {
      static_cast<void>(resizeterm(14, 80));
      auto cached_snapshot = snapshot;
      cached_snapshot.width = 80;
      cached_snapshot.height = 14;
      cached_snapshot.transcript.clear();
      cached_snapshot.input = "UPPER-CACHE-ROW\nLOWER-CACHE-ROW";
      cached_snapshot.input_cursor = cached_snapshot.input.size();
      ava::tui::detail::CompletionMatchCache completion_cache;
      ava::tui::detail::TranscriptLayoutCache transcript_cache;
      ava::tui::detail::ScreenRowCache screen_cache;
      if (!ava::tui::detail::draw_screen_cached(cached_snapshot, completion_cache, cached_snapshot.file_references_generation, transcript_cache,
                                                cached_snapshot.transcript_generation, screen_cache))
      {
        return false;
      }

      auto const initial_surfaces = screen_cache.surfaces;
      auto const upper = std::ranges::find_if(initial_surfaces, [](std::string const& line) { return line.find("UPPER-CACHE-ROW") != std::string::npos; });
      auto const lower = std::ranges::find_if(initial_surfaces, [](std::string const& line) { return line.find("LOWER-CACHE-ROW") != std::string::npos; });
      if (upper == initial_surfaces.end() || lower == initial_surfaces.end())
        return false;
      auto const upper_row = static_cast<std::size_t>(std::distance(initial_surfaces.begin(), upper));
      auto const lower_row = static_cast<std::size_t>(std::distance(initial_surfaces.begin(), lower));
      auto const initial_lower_screen_row = ncurses_screen_row(lower_row);
      if (lower_row != upper_row + 1 || !initial_lower_screen_row || initial_lower_screen_row->find("LOWER-CACHE-ROW") == std::string::npos)
        return false;

      cached_snapshot.input.replace(0, std::string_view("UPPER-CACHE-ROW").size(), "EDITD-CACHE-ROW");
      if (!ava::tui::detail::draw_screen_cached(cached_snapshot, completion_cache, cached_snapshot.file_references_generation, transcript_cache,
                                                cached_snapshot.transcript_generation, screen_cache))
      {
        return false;
      }
      auto const changed_rows = ava::tui::detail::changed_screen_rows(initial_surfaces, screen_cache.surfaces, {}, false);
      auto const retained_lower_screen_row = ncurses_screen_row(lower_row);
      return changed_rows == std::vector<std::size_t>{upper_row} && retained_lower_screen_row &&
             retained_lower_screen_row->find("LOWER-CACHE-ROW") != std::string::npos;
    };
    result.cached_row_draw_preserves_unchanged_lower_row = exercise_cached_row_draw();

    auto exercise_graphic_overlay_cache = [&]() {
      if (profile.no_color)
        return true;
      static_cast<void>(resizeterm(14, 80));
      auto graphic_snapshot = snapshot;
      graphic_snapshot.width = 80;
      graphic_snapshot.height = 14;
      graphic_snapshot.pending_attachments = {ava::tui::PendingAttachmentItem{
          .label = "cached-preview.png",
          .detail = "(image/png) persistent graphic cache regression",
          .preview = ava::tui::PendingAttachmentItem::Preview{.protocol = ava::tui::TerminalImageProtocol::Kitty,
                                                              .base64_data = std::make_shared<std::string const>("GRAPHIC_CACHE_FIRST_PAYLOAD"),
                                                              .dimensions = ava::tui::ImageDimensions{.width_px = 20, .height_px = 20},
                                                              .image_id = 424242}}};
      ava::tui::detail::CompletionMatchCache completion_cache;
      ava::tui::detail::TranscriptLayoutCache transcript_cache;
      ava::tui::detail::ScreenRowCache screen_cache;
      auto const saved_stdout = dup(STDOUT_FILENO);
      if (saved_stdout < 0 || std::fflush(stdout) != 0 || dup2(fileno(output), STDOUT_FILENO) < 0)
      {
        if (saved_stdout >= 0)
          static_cast<void>(close(saved_stdout));
        return false;
      }
      auto restore_stdout = [&]() {
        static_cast<void>(std::fflush(stdout));
        static_cast<void>(dup2(saved_stdout, STDOUT_FILENO));
        static_cast<void>(close(saved_stdout));
      };
      auto draw_and_capture = [&]() -> std::optional<std::string> {
        if (std::fflush(stdout) != 0 || std::fflush(output) != 0 || std::fseek(output, 0, SEEK_END) != 0)
          return std::nullopt;
        auto const before = std::ftell(output);
        if (before < 0 ||
            !ava::tui::detail::draw_screen_cached(graphic_snapshot, completion_cache, graphic_snapshot.file_references_generation, transcript_cache,
                                                  graphic_snapshot.transcript_generation, screen_cache) ||
            std::fflush(stdout) != 0 || std::fflush(output) != 0)
        {
          return std::nullopt;
        }
        auto const after = std::ftell(output);
        if (after < before || std::fseek(output, before, SEEK_SET) != 0)
          return std::nullopt;
        std::string captured(static_cast<std::size_t>(after - before), '\0');
        if (!captured.empty() && std::fread(captured.data(), 1, captured.size(), output) != captured.size())
          return std::nullopt;
        if (std::fseek(output, 0, SEEK_END) != 0)
          return std::nullopt;
        return captured;
      };

      auto const first = draw_and_capture();
      auto const identical = draw_and_capture();
      graphic_snapshot.pending_attachments.front().preview->base64_data = std::make_shared<std::string const>("GRAPHIC_CACHE_CHANGED_PAYLOAD");
      auto const changed = draw_and_capture();
      screen_cache.valid = false;
      auto const invalidated = draw_and_capture();
      graphic_snapshot.pending_attachments.clear();
      auto const removed = draw_and_capture();
      restore_stdout();
      auto const deletion = ava::tui::delete_kitty_image(424242);
      return first && identical && changed && invalidated && removed && first->find("GRAPHIC_CACHE_FIRST_PAYLOAD") != std::string::npos &&
             identical->find("GRAPHIC_CACHE_FIRST_PAYLOAD") == std::string::npos && changed->find("GRAPHIC_CACHE_CHANGED_PAYLOAD") != std::string::npos &&
             invalidated->find("GRAPHIC_CACHE_CHANGED_PAYLOAD") != std::string::npos && removed->find(deletion) != std::string::npos;
    };
    result.graphic_overlay_cache_stable = exercise_graphic_overlay_cache();

    auto exercise_processing_footer = [&](ava::tui::ComposerSnapshot footer_snapshot) {
      static_cast<void>(resizeterm(static_cast<int>(footer_snapshot.height), static_cast<int>(footer_snapshot.width)));
      footer_snapshot.processing = true;
      footer_snapshot.input = "cursor";
      footer_snapshot.input_cursor = footer_snapshot.input.size();
      footer_snapshot.pending_attachments = {ava::tui::PendingAttachmentItem{
          .label = "footer-preview.png",
          .detail = "(image/png) footer-only graphic regression",
          .preview = ava::tui::PendingAttachmentItem::Preview{.protocol = ava::tui::TerminalImageProtocol::Kitty,
                                                              .base64_data = std::make_shared<std::string const>("FOOTER_ONLY_KITTY_MARKER"),
                                                              .dimensions = ava::tui::ImageDimensions{.width_px = 20, .height_px = 20},
                                                              .image_id = 31337}}};
      auto const saved_stdout = dup(STDOUT_FILENO);
      if (saved_stdout < 0 || std::fflush(stdout) != 0 || dup2(fileno(output), STDOUT_FILENO) < 0)
      {
        if (saved_stdout >= 0)
          static_cast<void>(close(saved_stdout));
        return false;
      }
      auto restore_stdout = [&]() {
        static_cast<void>(std::fflush(stdout));
        static_cast<void>(dup2(saved_stdout, STDOUT_FILENO));
        static_cast<void>(close(saved_stdout));
      };
      if (!ava::tui::draw_screen(footer_snapshot))
      {
        restore_stdout();
        return false;
      }
      static_cast<void>(std::fflush(output));
      auto const output_before_footer = std::ftell(output);
      auto const footer_canvas = ava::tui::composer_canvas_layout(footer_snapshot);
      auto const expected_footer_column = footer_canvas.left + ava::tui::detail::input_cursor_column(footer_snapshot, footer_canvas.content_width);
      ava::tui::detail::CompletionMatchCache completion_cache;
      ava::tui::detail::TranscriptLayoutCache transcript_cache;
      ava::tui::detail::ScreenRowCache screen_cache;
      for (std::size_t frame = 0; frame != 4; ++frame)
      {
        footer_snapshot.spinner_frame = frame;
        if (!ava::tui::detail::draw_processing_footer_cached(footer_snapshot, completion_cache, footer_snapshot.file_references_generation, transcript_cache,
                                                             footer_snapshot.transcript_generation, screen_cache))
        {
          restore_stdout();
          return false;
        }
        int footer_cursor_y = 0;
        int footer_cursor_x = 0;
        getyx(stdscr, footer_cursor_y, footer_cursor_x);
        if (footer_cursor_x != static_cast<int>(expected_footer_column - 1) || footer_cursor_y < 0 || footer_cursor_y >= LINES)
        {
          restore_stdout();
          return false;
        }
      }
      static_cast<void>(std::fflush(output));
      auto const output_after_footer = std::ftell(output);
      std::string footer_output;
      bool footer_output_read = false;
      if (output_before_footer >= 0 && output_after_footer >= output_before_footer && std::fseek(output, output_before_footer, SEEK_SET) == 0)
      {
        footer_output.resize(static_cast<std::size_t>(output_after_footer - output_before_footer));
        footer_output_read = std::fread(footer_output.data(), 1, footer_output.size(), output) == footer_output.size();
        static_cast<void>(std::fseek(output, 0, SEEK_END));
      }
      restore_stdout();
      auto const footer_output_is_quiet = footer_output_read && footer_output.find("\x1b[?25l") == std::string::npos &&
                                          footer_output.find("\x1b[?25h") == std::string::npos && footer_output.find("\x1b[2J") == std::string::npos &&
                                          footer_output.find("FOOTER_ONLY_KITTY_MARKER") == std::string::npos;
      auto const footer_output_is_plain = !profile.no_color || (footer_output_read && footer_output.find("\x1b[1m") == std::string::npos);
      result.processing_footer_output_is_quiet = result.processing_footer_output_is_quiet && footer_output_is_quiet;
      result.processing_footer_output_is_plain = result.processing_footer_output_is_plain && footer_output_is_plain;
      return output_before_footer >= 0 && output_after_footer >= output_before_footer && footer_output_is_quiet && footer_output_is_plain;
    };
    auto ordinary_footer_snapshot = snapshot;
    ordinary_footer_snapshot.width = 80;
    ordinary_footer_snapshot.height = 14;
    auto centered_footer_snapshot = snapshot;
    centered_footer_snapshot.width = 160;
    centered_footer_snapshot.height = 14;
    auto rail_footer_snapshot = centered_footer_snapshot;
    rail_footer_snapshot.sidebar = ava::tui::SidebarSnapshot{.activity = {ava::tui::SidebarActivityItem{.id = "running", .label = "run", .detail = "active"}}};
    auto narrow_footer_snapshot = ordinary_footer_snapshot;
    narrow_footer_snapshot.width = 20;
    narrow_footer_snapshot.height = 8;
    result.processing_footer_output_is_quiet = true;
    result.processing_footer_output_is_plain = true;
    result.processing_footer_updates_stable = exercise_processing_footer(ordinary_footer_snapshot) && exercise_processing_footer(centered_footer_snapshot) &&
                                              exercise_processing_footer(rail_footer_snapshot) && exercise_processing_footer(narrow_footer_snapshot);
    result.cursor_forced_visible_for_teardown = ava::tui::detail::force_terminal_cursor_visible();
    static_cast<void>(endwin());
    delscreen(screen);
  }

  static_cast<void>(std::fclose(input));
  static_cast<void>(std::fclose(output));
  return result;
}

void test_ncurses_newterm_smoke_without_real_tty()
{
  char const* previous_locale_value = std::setlocale(LC_ALL, nullptr);
  std::string const previous_locale = previous_locale_value == nullptr ? "C" : previous_locale_value;
  static_cast<void>(std::setlocale(LC_ALL, ""));

  std::vector<VirtualTerminalProfile> const profiles = {
      {.name = "xterm baseline", .term = "xterm-256color"},
      {.name = "xterm NO_COLOR", .term = "xterm-256color", .no_color = true},
      {.name = "screen/tmux terminfo", .term = "screen-256color"},
      {.name = "tmux-like environment",
       .term = "xterm-256color",
       .term_program = "tmux",
       .colorterm = "truecolor",
       .tmux = "/tmp/tmux-1000/default,123,0",
       .tmux_pane = "%1"},
      {.name = "kitty-like environment", .term = "xterm-256color", .term_program = "kitty", .colorterm = "truecolor", .kitty_window_id = "1"},
      {.name = "wezterm over ssh-like environment",
       .term = "xterm-256color",
       .term_program = "WezTerm",
       .colorterm = "truecolor",
       .ssh_tty = "/dev/pts/99",
       .wezterm_exec = "/usr/bin/wezterm"}};

  std::size_t exercised = 0;
  bool checked_default_screen_bg = false;
  for (auto const& profile : profiles)
  {
    auto const result = exercise_virtual_terminal_profile(profile);
    if (result.screen_created)
      ++exercised;
    expect(result.screen_created, "ncurses smoke test creates a screen without a real terminal for " + profile.name);
    expect(result.base_drawn && result.modal_drawn && result.cursor_restored_after_modal && result.cached_row_draw_preserves_unchanged_lower_row &&
               result.graphic_overlay_cache_stable && result.processing_footer_updates_stable && result.processing_footer_output_is_quiet &&
               result.processing_footer_output_is_plain && result.cursor_forced_visible_for_teardown &&
               (!result.checked_builtin_dark_default_screen_bg || result.builtin_dark_stdscr_background_is_default),
           "ncurses smoke test draws base/modal frames, preserves unchanged rows, suppresses identical graphic payloads while retransmitting changes and "
           "invalidations and deleting removed Kitty images, updates processing footers without terminal clears, cursor toggles, graphics, or NO_COLOR bold "
           "styling, forces the cursor visible for teardown, and keeps the built-in dark stdscr background pair at terminal default (-1) for " +
               profile.name);
    if (result.checked_builtin_dark_default_screen_bg)
      checked_default_screen_bg = true;
  }
  expect(checked_default_screen_bg,
         "ncurses smoke test verifies built-in dark stdscr background uses terminal default color on one supported baseline profile");
  expect(exercised == profiles.size(),
         "ncurses smoke test covers xterm and screen terminfo plus tmux, kitty, wezterm, and ssh-like environment "
         "variables");
  static_cast<void>(std::setlocale(LC_ALL, previous_locale.c_str()));
}
}  // namespace

void run_tui_terminal_virtual_smoke_tests()
{
  test_ncurses_newterm_smoke_without_real_tty();
}
