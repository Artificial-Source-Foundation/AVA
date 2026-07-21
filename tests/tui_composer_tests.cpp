#include "sys.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/command_palette.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/event_state.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/runtime.h"
#include "ava/tui/session_grants.h"
#include "ava/tui/terminal.h"
#include "ava/tui/terminal_image.h"
#include "ava/tui/text_wrap.h"
#include "ava/tui/theme.h"
#include "ava/tui/tool_cards.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/session/session_tree.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <curses.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct ScopedTerminalCapabilityProfile
{
  explicit ScopedTerminalCapabilityProfile(std::string term_program_value)
      : term("TERM", "xterm-256color"),
        term_program("TERM_PROGRAM", std::move(term_program_value)),
        terminal_emulator("TERMINAL_EMULATOR", ""),
        tmux("TMUX", ""),
        kitty_window_id("KITTY_WINDOW_ID", ""),
        ghostty_resources_dir("GHOSTTY_RESOURCES_DIR", ""),
        wezterm_pane("WEZTERM_PANE", ""),
        warp_session_id("WARP_SESSION_ID", ""),
        warp_terminal_session_uuid("WARP_TERMINAL_SESSION_UUID", ""),
        iterm_session_id("ITERM_SESSION_ID", ""),
        wt_session("WT_SESSION", ""),
        tmux_hyperlinks("AVA_TUI_TMUX_HYPERLINKS", "")
  {
  }

  ScopedEnvVar term;
  ScopedEnvVar term_program;
  ScopedEnvVar terminal_emulator;
  ScopedEnvVar tmux;
  ScopedEnvVar kitty_window_id;
  ScopedEnvVar ghostty_resources_dir;
  ScopedEnvVar wezterm_pane;
  ScopedEnvVar warp_session_id;
  ScopedEnvVar warp_terminal_session_uuid;
  ScopedEnvVar iterm_session_id;
  ScopedEnvVar wt_session;
  ScopedEnvVar tmux_hyperlinks;
};

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

void test_tui_composer_rendering_and_input()
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

  auto prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::Tab});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt tab toggles focus to allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Tab});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Deny,
         "permission prompt tab toggles focus back to deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::ArrowLeft});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Deny,
         "permission prompt left arrow selects deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::ArrowRight});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt right arrow selects allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::ArrowUp});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Deny,
         "permission prompt up arrow selects the previous action consistently with list modals");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::ArrowDown});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt down arrow selects the next action consistently with list modals");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllow, "permission prompt enter confirms selected allow");
  prompt_input =
      ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = ' '});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny, "permission prompt space confirms selected deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Space});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllow, "permission prompt semantic Space confirms the selected choice");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Escape});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny, "permission prompt escape resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::CtrlC});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny, "permission prompt ctrl-c resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::CtrlD});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny, "permission prompt ctrl-d resolves deny");
  prompt_input =
      ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'A'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllow, "permission prompt A resolves allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'D'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDeny, "permission prompt D resolves deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'x'});
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::None && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt ignores unmapped character keys without changing focus");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'S'}, false);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::None && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt ignores session shortcut when session grant is unavailable");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'R'}, false, false, false);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::None && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt ignores remembered-rule shortcut when rule storage is unavailable");
  prompt_input =
      ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Tab}, false, true, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::DenyRemember,
         "permission prompt cycles to remembered deny when rule storage is available");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::DenyRemember, ava::tui::InputEvent{.key = ava::tui::Key::Enter},
                                                          false, true, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveDenyRemember, "permission prompt enter confirms remembered deny");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'R'}, false, true, true);
  expect(
      prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::AllowRemember,
      "permission prompt R toggles the selected allow choice into a remembered allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::AllowRemember, ava::tui::InputEvent{.key = ava::tui::Key::Enter},
                                                          false, true, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllowRemember, "permission prompt enter confirms remembered allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Deny,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'R'}, false, false, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::DenyRemember,
         "permission prompt keeps remembered deny available when a Critical command cannot be remembered as allow");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'R'}, false, false, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::None && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::Allow,
         "permission prompt does not expose remembered allow when the backend only permits one-shot approval");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'S'}, true, true, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::ResolveAllowSession &&
             prompt_input.selected_choice == ava::tui::PermissionPromptChoice::AllowSession,
         "permission prompt S resolves allow session when session grant is available");
  prompt_input =
      ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::Allow, ava::tui::InputEvent{.key = ava::tui::Key::Tab}, true, true, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::AllowSession,
         "permission prompt tab advances from allow to allow session when available");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::AllowSession, ava::tui::InputEvent{.key = ava::tui::Key::Tab}, true,
                                                          true, true);
  expect(prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::DenyRemember,
         "permission prompt tab advances from allow session to remembered deny when available");
  prompt_input = ava::tui::handle_permission_prompt_input(ava::tui::PermissionPromptChoice::AllowSession,
                                                          ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'R'}, true, true, true);
  expect(
      prompt_input.action == ava::tui::PermissionPromptInputAction::Redraw && prompt_input.selected_choice == ava::tui::PermissionPromptChoice::AllowRemember,
      "permission prompt R toggles allow session into remembered allow");
  ava::permissions::CommandPermissionMetadata one_shot_metadata;
  one_shot_metadata.level = ava::command::CommandLevel::Critical;
  one_shot_metadata.backend_maximum_scope = ava::command::InteractiveScope::Once;
  ava::permissions::PermissionPrompt one_shot_prompt;
  one_shot_prompt.operation = ava::permissions::Operation::RunCommand;
  one_shot_prompt.command_metadata = one_shot_metadata;
  auto const one_shot_remember_availability = ava::tui::permission_prompt_remember_availability(one_shot_prompt, true);
  auto const unavailable_storage_remember_availability = ava::tui::permission_prompt_remember_availability(one_shot_prompt, false);
  expect(!one_shot_remember_availability.allow_remember_available && one_shot_remember_availability.deny_remember_available &&
             !unavailable_storage_remember_availability.allow_remember_available && !unavailable_storage_remember_availability.deny_remember_available,
         "tui runtime enables a persistent deny but not a persistent allow for one-shot Critical prompts when rule storage exists");

  auto single_question = ava::tui::QuestionPromptView{
      .header = "Choose",
      .question = "Pick one",
      .options = {ava::tui::QuestionPromptOptionView{.value = "alpha", .label = "Alpha"}, ava::tui::QuestionPromptOptionView{.value = "beta", .label = "Beta"}},
      .multiple = false,
      .allow_custom = true,
      .selected_option_index = 0,
      .custom_text = ""};
  auto question_input = ava::tui::handle_question_prompt_input(single_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = '2'});
  expect(
      question_input.action == ava::tui::QuestionPromptInputAction::Resolve && question_input.selected_option_index == 1 && question_input.options[1].selected,
      "question prompt numeric shortcut selects and resolves a single-select option");
  question_input = ava::tui::handle_question_prompt_input(single_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'x'});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text == "x" &&
             std::ranges::none_of(question_input.options, [](ava::tui::QuestionPromptOptionView const& option) { return option.selected; }),
         "question prompt custom text edits clear single-select option state");
  single_question.custom_text = question_input.custom_text;
  question_input = ava::tui::handle_question_prompt_input(single_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = ' '});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text == "x ",
         "question prompt custom text can include spaces after typing starts");
  single_question.custom_text = question_input.custom_text;
  question_input = ava::tui::handle_question_prompt_input(single_question, ava::tui::InputEvent{.key = ava::tui::Key::Space});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text == "x  ",
         "question prompt semantic Space appends to existing custom text");
  single_question.custom_text = question_input.custom_text;
  question_input = ava::tui::handle_question_prompt_input(
      single_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = static_cast<char>(0xC3), .text = "\xC3\xA9"});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text == std::string("x  ") + "\xC3\xA9",
         "question prompt custom text preserves utf-8 input");
  single_question.custom_text = "x";
  question_input = ava::tui::handle_question_prompt_input(single_question, ava::tui::InputEvent{.key = ava::tui::Key::Backspace});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text.empty(),
         "question prompt backspace edits custom text");
  auto secret_question = ava::tui::QuestionPromptView{.header = "Connect",
                                                      .question = "Paste API key",
                                                      .options = {},
                                                      .multiple = false,
                                                      .allow_custom = true,
                                                      .secret = true,
                                                      .selected_option_index = 0,
                                                      .custom_text = ""};
  question_input = ava::tui::handle_question_prompt_input(secret_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.custom_text.empty(),
         "question prompt enter keeps an empty single custom answer open");

  auto copy_question = ava::tui::QuestionPromptView{.header = "Connect",
                                                    .question = "Open URL",
                                                    .options = {ava::tui::QuestionPromptOptionView{.value = "done", .label = "Done"},
                                                                ava::tui::QuestionPromptOptionView{.value = "copy:https://auth.openai.com", .label = "C Copy"}},
                                                    .multiple = false,
                                                    .allow_custom = false,
                                                    .selected_option_index = 0,
                                                    .custom_text = ""};
  question_input = ava::tui::handle_question_prompt_input(copy_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'c'});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Copy && question_input.copy_text == "https://auth.openai.com" &&
             std::ranges::none_of(question_input.options, [](ava::tui::QuestionPromptOptionView const& option) { return option.selected; }),
         "question prompt copy shortcut copies without resolving a single-select option");
  question_input = ava::tui::handle_question_prompt_input(copy_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Resolve && question_input.options[0].selected,
         "question prompt enter confirms the selected non-copy option");

  auto multi_question = ava::tui::QuestionPromptView{
      .header = "Choose",
      .question = "Pick many",
      .options = {ava::tui::QuestionPromptOptionView{.value = "read", .label = "Read"}, ava::tui::QuestionPromptOptionView{.value = "grep", .label = "Grep"}},
      .multiple = true,
      .allow_custom = true,
      .selected_option_index = 0,
      .custom_text = ""};
  question_input = ava::tui::handle_question_prompt_input(multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = ' '});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && question_input.options[0].selected,
         "question prompt space toggles selected multi-select option");
  multi_question.options = question_input.options;
  question_input = ava::tui::handle_question_prompt_input(multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Space});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && !question_input.options[0].selected,
         "question prompt semantic Space toggles selected multi-select option");
  multi_question.options = question_input.options;
  question_input = ava::tui::handle_question_prompt_input(multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = '2'});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Redraw && !question_input.options[0].selected && question_input.options[1].selected,
         "question prompt numeric shortcut toggles multi-select options without resolving");
  multi_question.options = question_input.options;
  question_input = ava::tui::handle_question_prompt_input(multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Resolve && !question_input.options[0].selected && question_input.options[1].selected,
         "question prompt enter resolves current multi-select choices");
  question_input = ava::tui::handle_question_prompt_input(multi_question, ava::tui::InputEvent{.key = ava::tui::Key::Escape});
  expect(question_input.action == ava::tui::QuestionPromptInputAction::Cancel, "question prompt escape cancels safely");
  auto empty_multi_question = multi_question;
  for (auto& option : empty_multi_question.options) option.selected = false;
  empty_multi_question.custom_text.clear();
  auto empty_multi_answer = ava::tui::question_answer_from_prompt_view(empty_multi_question);
  expect(empty_multi_answer && empty_multi_answer->selected_options.empty() && empty_multi_answer->custom_text.empty(),
         "question prompt accepts an empty multi-select answer");

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
             draft.kill_buffer == "line",
         "tui draft editor deletes following text after a line-end join");
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
             ring_draft.kill_buffer == "beta ",
         "tui draft editor keeps the most recent kill at the front of the ring");
  expect(ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::Yank) && ring_draft.text == "alpha beta ",
         "tui draft editor yanks the newest kill-ring entry");
  expect(
      ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::YankPop) && ring_draft.text == "alpha gamma" && ring_draft.kill_buffer == "gamma",
      "tui draft editor yank-pop swaps the previous yank with the next kill-ring entry");
  expect(
      ava::tui::apply_composer_draft_action(ring_draft, ava::tui::TuiAction::YankPop) && ring_draft.text == "alpha beta " && ring_draft.kill_buffer == "beta ",
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

  auto const split_empty = ava::tui::split_lines("");
  expect(split_empty.size() == 1 && split_empty.front().empty(), "tui split keeps empty input as one line");
  auto const split_trailing = ava::tui::split_lines("a\n");
  expect(split_trailing.size() == 2 && split_trailing[0] == "a" && split_trailing[1].empty(), "tui split preserves trailing empty line");
  auto const split_crlf = ava::tui::split_lines("a\r\nb\rc");
  expect(split_crlf.size() == 3 && split_crlf[0] == "a" && split_crlf[1] == "b" && split_crlf[2] == "c",
         "tui split treats crlf and carriage-return output as line breaks");

  auto const lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "/help",
      .status = "slash palette dismissed",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"}, ava::tui::TranscriptItem{.label = "ava", .text = "world"}},
      .width = 80,
      .height = 14});
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "1");
    auto const plain_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{
        .mode = "build",
        .provider = "openai",
        .model = "gpt-5.5",
        .session_id = "session_plain",
        .input = "/model",
        .status = "ready",
        .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Plain output keeps **bold**, `code`, and colors out of the terminal"}},
        .select_list = ava::tui::SelectListView{.title = "Select model",
                                                .subtitle = "NO_COLOR smoke",
                                                .items = {ava::tui::SelectListItemView{.value = "openai/gpt-5.5",
                                                                                       .label = "GPT-5.5",
                                                                                       .description = "openai/gpt-5.5",
                                                                                       .group = "openai",
                                                                                       .detail = "plain terminal",
                                                                                       .badge = "current",
                                                                                       .current = true,
                                                                                       .enabled = true,
                                                                                       .disabled_reason = {}}},
                                                .selected_item_index = 0,
                                                .query = {},
                                                .placeholder = "Search models",
                                                .empty_text = "No models",
                                                .footer_hint = "Enter choose · Esc cancel"},
        .width = 88,
        .height = 18,
        .input_cursor = 6});
    expect(std::ranges::all_of(plain_lines, [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 88; }) &&
               std::ranges::any_of(plain_lines, [](std::string const& line) { return line.find("Select model") != std::string::npos; }) &&
               std::ranges::any_of(plain_lines,
                                   [](std::string const& line) { return line.find("Plain output keeps bold, code, and colors") != std::string::npos; }),
           "tui honors NO_COLOR by rendering the full frame without ANSI styling while preserving visible content and width");
  }
  expect(lines.size() == 14, "tui fills the viewport with transcript, spacer, and composer lines");
  expect(!lines.empty() && strip_sgr(lines.front()).find("hello") != std::string::npos, "tui starts short chats at the top of the transcript area");
  expect(lines.size() == 14 && strip_sgr(lines[12]).starts_with("│  /help") && strip_sgr(lines[13]).starts_with("│  GPT-5.5") &&
             lines[11].find("\x1b[48;2;26;31;46m") == std::string::npos &&
             std::ranges::none_of(lines, [](std::string const& line) { return line.find("/ commands") != std::string::npos; }),
         "tui keeps a one-line draft in exactly the bottom input and footer rows without composer-surface padding");
  expect(std::ranges::any_of(lines, [](std::string const& line) { return strip_sgr(line).find("│  /help") != std::string::npos; }),
         "tui renders the quiet composer input without a prompt glyph");
  expect(std::ranges::none_of(lines, [](std::string const& line) { return strip_sgr(line).find("slash palette dismissed") != std::string::npos; }),
         "tui keeps transient composer status text out of the footer");
  expect(std::ranges::any_of(lines,
                             [](std::string const& line) {
                               return line.find("\x1b[48;2;26;31;46m") != std::string::npos && line.find("\x1b[38;2;77;158;246m│") != std::string::npos &&
                                      strip_sgr(line).find("❯") == std::string::npos;
                             }),
         "tui preserves the elevated composer surface with one quiet accent boundary");
  expect(std::ranges::none_of(lines, [](std::string const& line) { return strip_sgr(line).find("╭─ You") != std::string::npos; }) &&
             std::ranges::none_of(lines, [](std::string const& line) { return strip_sgr(line).find("╭─ AVA") != std::string::npos; }) &&
             std::ranges::any_of(lines,
                                 [](std::string const& line) {
                                   return line.find("\x1b[48;2;26;31;46m") != std::string::npos && strip_sgr(line).find("hello") != std::string::npos;
                                 }) &&
             std::ranges::any_of(lines, [](std::string const& line) { return strip_sgr(line).find("world") != std::string::npos; }),
         "tui renders user messages as highlighted input blocks and assistant messages without role headers");

  auto const idle_two_row_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                .provider = "openai",
                                                                .model = "gpt-5.5",
                                                                .session_id = "session_test",
                                                                .input = "",
                                                                .status = "ready",
                                                                .context_source_count = 2,
                                                                .transcript = {},
                                                                .width = 80,
                                                                .height = 10};
  auto const idle_two_row_lines = ava::tui::render_composer(idle_two_row_snapshot);
  auto idle_input = strip_sgr(idle_two_row_lines[8]);
  auto idle_footer = strip_sgr(idle_two_row_lines[9]);
  while (!idle_input.empty() && idle_input.back() == ' ') idle_input.pop_back();
  while (!idle_footer.empty() && idle_footer.back() == ' ') idle_footer.pop_back();
  expect(idle_two_row_lines.size() == 10 && idle_input == "│  Type a message..." && idle_footer == "│  GPT-5.5 · ctx 2" &&
             idle_two_row_lines[7].find("\x1b[48;2;26;31;46m") == std::string::npos &&
             std::ranges::count_if(idle_two_row_lines, [](std::string const& line) { return line.find("\x1b[48;2;26;31;46m") != std::string::npos; }) == 2 &&
             std::ranges::none_of(idle_two_row_lines, [](std::string const& line) { return strip_sgr(line).find("❯") != std::string::npos; }),
         "tui empty composer is exactly two bottom rows with one boundary, quiet gutter, pure footer, and no prompt glyph");
  auto const composer_lines_for = [&](std::string input, std::size_t width = 80) {
    auto snapshot = idle_two_row_snapshot;
    snapshot.input = std::move(input);
    return ava::tui::detail::composer_block_line_count(snapshot, 100, width);
  };
  expect(composer_lines_for("") == 2 && composer_lines_for("one") == 2 && composer_lines_for("one\ntwo") == 3 &&
             composer_lines_for("abcdefghijklmnopqr", 20) == 3 && composer_lines_for("1\n2\n3\n4\n5\n6\n7") == 8 &&
             composer_lines_for("1\n2\n3\n4\n5\n6\n7\n8\n9") == 8,
         "tui composer desired height is visible input lines plus one footer bounded to two through eight rows");

  auto const processing_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "",
                                                                                     .status = "thinking...",
                                                                                     .processing = true,
                                                                                     .spinner_frame = 1,
                                                                                     .token_status = "1.3k (0.7%)",
                                                                                     .transcript = {},
                                                                                     .width = 80,
                                                                                     .height = 10});
  expect(processing_lines.size() == 10 && strip_sgr(processing_lines[8]).starts_with("│  Type a message...") &&
             strip_sgr(processing_lines[9]).starts_with("│  GPT-5.5") && strip_sgr(processing_lines[9]).find("⠙") != std::string::npos &&
             processing_lines[7].find("\x1b[48;2;26;31;46m") == std::string::npos &&
             std::ranges::all_of(processing_lines,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("thinking...") == std::string::npos && visible.find("working") == std::string::npos &&
                                          visible.find("1.3k (0.7%)") == std::string::npos && visible.find("❯") == std::string::npos;
                                 }),
         "tui processing composer stays two rows and keeps only model metadata plus the active spinner in the footer");

  auto narrow_footer_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.6-terra",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .context_source_count = 12,
                                                           .transcript = {},
                                                           .width = 20,
                                                           .height = 8};
  auto const narrow_footer_lines = ava::tui::render_composer(narrow_footer_snapshot);
  expect(std::ranges::any_of(narrow_footer_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-") != std::string::npos && visible.find("ctx 12") != std::string::npos && visible_columns(line) == 20;
                             }),
         "tui shortens a long model label before dropping multi-digit context metadata at the supported minimum width");

  narrow_footer_snapshot.status = "thinking...";
  narrow_footer_snapshot.processing = true;
  narrow_footer_snapshot.spinner_frame = 1;
  auto const narrow_processing_lines = ava::tui::render_composer(narrow_footer_snapshot);
  expect(std::ranges::any_of(narrow_processing_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-") != std::string::npos && visible.find("ctx 12") != std::string::npos &&
                                      visible.find("⠙") != std::string::npos && visible_columns(line) == 20;
                             }),
         "tui preserves multi-digit context metadata and the spinner beside a shortened model at the supported minimum width");

  auto const queued_lines = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "thinking...",
                                 .processing = true,
                                 .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "work on queue UI"}},
                                 .width = 80,
                                 .height = 12,
                                 .queued_messages = {ava::tui::QueuedMessageItem{.id = "q1", .kind = "follow-up", .text = "run tests next"},
                                                     ava::tui::QueuedMessageItem{.id = "q2", .kind = "steer", .text = "keep patch small"}}});
  expect(std::ranges::any_of(queued_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("queued follow-up run tests next") != std::string::npos;
                             }) &&
             std::ranges::any_of(queued_lines,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("queued steer keep patch small") != std::string::npos &&
                                          visible.find("/restore or Alt+Up latest") != std::string::npos;
                                 }),
         "tui renders active-run queued steering/follow-up messages in a compact pending region");

  auto const attachment_lines = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "attached image for next prompt",
                                 .transcript = {ava::tui::TranscriptItem{.label = "status", .text = "attached image"}},
                                 .width = 80,
                                 .height = 12,
                                 .pending_attachments = {ava::tui::PendingAttachmentItem{.label = "screen.png", .detail = "(image/png, 68 bytes)"}}});
  expect(std::ranges::any_of(attachment_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("attached image screen.png") != std::string::npos &&
                                      visible.find("(image/png, 68 bytes)") != std::string::npos && visible.find("(next prompt)") != std::string::npos;
                             }),
         "tui renders pending image attachments before the next prompt is submitted");
  auto const attachment_preview_snapshot = ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "attached image for next prompt",
      .transcript = {ava::tui::TranscriptItem{.label = "status", .text = "attached image"}},
      .width = 80,
      .height = 14,
      .pending_attachments = {ava::tui::PendingAttachmentItem{
          .label = "screen.png",
          .detail = "(image/png, 68 bytes) preview kitty",
          .preview = ava::tui::PendingAttachmentItem::Preview{.protocol = ava::tui::TerminalImageProtocol::Kitty,
                                                              .base64_data = std::make_shared<std::string const>("AAAA"),
                                                              .dimensions = ava::tui::ImageDimensions{.width_px = 20, .height_px = 20},
                                                              .image_id = 42}}}};
  auto const attachment_preview_frame = ava::tui::render_composer_frame(attachment_preview_snapshot);
  auto const attachment_preview_lines = ava::tui::render_composer(attachment_preview_snapshot);
  expect(attachment_preview_frame.graphics.size() == 1 && attachment_preview_frame.graphics[0].protocol == ava::tui::TerminalImageProtocol::Kitty &&
             attachment_preview_frame.graphics[0].image_id == std::optional<std::size_t>{42} && attachment_preview_frame.graphics[0].rows > 1 &&
             attachment_preview_frame.graphics[0].sequence.starts_with("\x1b_G") &&
             attachment_preview_frame.graphics[0].sequence.find("C=1") != std::string::npos &&
             std::ranges::none_of(attachment_preview_lines, [](std::string const& line) { return ava::tui::terminal_line_contains_image_sequence(line); }),
         "tui render frame reserves rows and carries trusted Kitty image graphics outside the text-only line API");
  {
    ScopedEnvVar no_color_preview_guard("NO_COLOR", "1");
    auto const plain_attachment_preview_frame = ava::tui::render_composer_frame(attachment_preview_snapshot);
    expect(plain_attachment_preview_frame.graphics.empty() && std::ranges::none_of(plain_attachment_preview_frame.lines,
                                                                                   [](std::string const& line) {
                                                                                     return line.find("\x1b[") != std::string::npos ||
                                                                                            ava::tui::terminal_line_contains_image_sequence(line);
                                                                                   }),
           "plain TUI output keeps image previews on the textual fallback path without ANSI or graphics escapes");
  }

  auto const reasoning_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "explain this",
                                                                                    .status = "ready",
                                                                                    .reasoning_status = "low",
                                                                                    .transcript = {},
                                                                                    .width = 80,
                                                                                    .height = 10});
  expect(std::ranges::any_of(reasoning_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5") != std::string::npos && visible.find("Build") == std::string::npos &&
                                      visible.find("OpenAI") == std::string::npos && visible.find("low") == std::string::npos;
                             }),
         "tui keeps mode, provider, and reasoning level out of the composer footer");

  auto const default_reasoning_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                            .provider = "openai",
                                                                                            .model = "gpt-5.5",
                                                                                            .session_id = "session_test",
                                                                                            .input = "explain this",
                                                                                            .status = "ready",
                                                                                            .transcript = {},
                                                                                            .width = 80,
                                                                                            .height = 10});
  expect(std::ranges::any_of(default_reasoning_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5") != std::string::npos && visible.find("Build") == std::string::npos &&
                                      visible.find("OpenAI") == std::string::npos && visible.find("default") == std::string::npos;
                             }),
         "tui renders only the model when context metadata is unavailable");
  expect(
      std::ranges::none_of(default_reasoning_lines, [](std::string const& line) { return strip_sgr(line).find("session session_test") != std::string::npos; }),
      "tui keeps the session id out of the composer footer");

  auto const plan_mode_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "plan",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 80,
                                                                                    .height = 10});
  expect(std::ranges::any_of(default_reasoning_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5") != std::string::npos && visible.find("Build") == std::string::npos;
                             }) &&
             std::ranges::any_of(plan_mode_lines,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("GPT-5.5") != std::string::npos && visible.find("Plan") == std::string::npos;
                                 }),
         "tui keeps build and plan mode badges out of the composer footer");

  auto const token_margin_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                       .provider = "openai",
                                                                                       .model = "gpt-5.5",
                                                                                       .session_id = "session_test",
                                                                                       .input = "",
                                                                                       .status = "ready",
                                                                                       .token_status = "1.3k (0.7%)",
                                                                                       .transcript = {},
                                                                                       .width = 80,
                                                                                       .height = 10});
  expect(std::ranges::none_of(token_margin_lines, [](std::string const& line) { return strip_sgr(line).find("1.3k (0.7%)") != std::string::npos; }),
         "tui keeps token-status text out of the composer footer");

  auto const compact_footer_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                         .provider = "openai",
                                                                                         .model = "gpt-5.5",
                                                                                         .session_id = "session_test",
                                                                                         .input = "",
                                                                                         .status = "ready",
                                                                                         .token_status = "1.3k (0.7%)",
                                                                                         .transcript = {},
                                                                                         .width = 110,
                                                                                         .height = 10,
                                                                                         .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                                              .mode = "build",
                                                                                                                              .provider = "openai",
                                                                                                                              .model = "gpt-5.5",
                                                                                                                              .workspace = "/workspace/project",
                                                                                                                              .git_branch = "develop",
                                                                                                                              .token_status = "1.3k (0.7%)",
                                                                                                                              .context_source_count = 2,
                                                                                                                              .session_entry_count = 42}});
  expect(std::ranges::any_of(compact_footer_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5 · ctx 2") != std::string::npos && visible.find("Build") == std::string::npos &&
                                      visible.find("OpenAI") == std::string::npos && visible.find("cwd") == std::string::npos &&
                                      visible.find("git") == std::string::npos && visible.find("entries") == std::string::npos &&
                                      visible.find("1.3k (0.7%)") == std::string::npos;
                             }),
         "tui compact footer shows only the model name and context source count");

  auto const refreshed_context_lines = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                            .provider = "openai",
                                                                                            .model = "gpt-5.5",
                                                                                            .session_id = "session_test",
                                                                                            .input = "",
                                                                                            .status = "ready",
                                                                                            .context_source_count = 3,
                                                                                            .transcript = {},
                                                                                            .width = 110,
                                                                                            .height = 10,
                                                                                            .sidebar = ava::tui::SidebarSnapshot{.context_source_count = 2}});
  expect(std::ranges::any_of(refreshed_context_lines,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("GPT-5.5 · ctx 3") != std::string::npos && visible.find("ctx 2") == std::string::npos;
                             }),
         "tui composer footer prefers refreshed runtime context count over stale sidebar metadata");

  auto const markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "First paragraph wraps cleanly across words.\n\nSecond paragraph stays separate.\n- bullet item\n* star "
                                                      "item\n1. numbered item\n> quoted text\n```cpp\nint main() {}\n```\nUse `ava build` and "
                                                      "**bold text**."}},
      .width = 72,
      .height = 24});
  expect(std::ranges::any_of(markdown_transcript,
                             [](std::string const& line) { return strip_sgr(line).find("First paragraph wraps cleanly") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript,
                                 [](std::string const& line) { return strip_sgr(line).find("Second paragraph stays separate") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("- bullet item") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("* star item") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("1. numbered item") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("│ quoted text") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("``` cpp") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("  int main() {}") != std::string::npos; }) &&
             std::ranges::any_of(markdown_transcript,
                                 [](std::string const& line) { return strip_sgr(line).find("Use ava build and bold text") != std::string::npos; }) &&
             std::ranges::none_of(markdown_transcript,
                                  [](std::string const& line) {
                                    auto const visible = strip_sgr(line);
                                    return visible.find("`ava build`") != std::string::npos || visible.find("**bold text**") != std::string::npos;
                                  }),
         "tui assistant renderer handles paragraphs, lists, quotes, fenced code, inline code, and bold");

  constexpr auto kBoldSgr = std::string_view{"\x1b[1m"};
  constexpr auto kItalicSgr = std::string_view{"\x1b[3m"};
  constexpr auto kUnderlineSgr = std::string_view{"\x1b[4m"};
  constexpr auto kStrikethroughSgr = std::string_view{"\x1b[9m"};
  constexpr auto kResetSgr = std::string_view{"\x1b[0m"};
  constexpr auto kMutedSgr = std::string_view{"\x1b[38;2;139;149;165m"};
  constexpr auto kWarningSgr = std::string_view{"\x1b[38;2;251;191;36m"};
  constexpr auto kSuccessSgr = std::string_view{"\x1b[38;2;52;211;153m"};
  constexpr auto kErrorSgr = std::string_view{"\x1b[38;2;248;113;113m"};
  constexpr auto kAccentSgr = std::string_view{"\x1b[38;2;77;158;246m"};

  auto const role_markup_transcript =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "Use `x` and **y**."},
                                                                          ava::tui::TranscriptItem{.label = "ava", .text = "Use `x` and **y**."}},
                                                           .width = 28,
                                                           .height = 16});
  auto const user_markup_line =
      std::ranges::find_if(role_markup_transcript, [](std::string const& line) { return strip_sgr(line).find("Use `x` and **y**.") != std::string::npos; });
  auto const assistant_markup_line =
      std::ranges::find_if(role_markup_transcript, [](std::string const& line) { return strip_sgr(line).find("Use x and y.") != std::string::npos; });
  expect(user_markup_line != role_markup_transcript.end() && assistant_markup_line != role_markup_transcript.end() &&
             !has_active_sgr_at_text(*user_markup_line, "x", kWarningSgr) && !has_active_sgr_at_text(*user_markup_line, "y", kBoldSgr) &&
             visible_columns(*user_markup_line) < 28 && has_active_sgr_at_text(*assistant_markup_line, "x", kWarningSgr) &&
             has_active_sgr_at_text(*assistant_markup_line, "y", kBoldSgr),
         "tui keeps user inline markdown literal inside a compact bubble while formatting assistant inline markdown");

  {
    ScopedTerminalCapabilityProfile no_hyperlink_terminal("");
    auto const inline_style_transcript = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Use *italic* and [docs](https://example.test)."}},
                                   .width = 72,
                                   .height = 12});
    auto const inline_style_line = std::ranges::find_if(inline_style_transcript, [](std::string const& line) {
      return strip_sgr(line).find("Use italic and docs (https://example.test).") != std::string::npos;
    });
    expect(inline_style_line != inline_style_transcript.end() && has_active_sgr_at_text(*inline_style_line, "italic", kItalicSgr) &&
               has_active_sgr_at_text(*inline_style_line, "docs", kUnderlineSgr),
           "tui assistant renderer emits visible italic and link underline SGR for parsed Markdown inline styling");

    auto const heading_inline_transcript = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "### Why `sourceInfo` should stay **visible**."}},
                                   .width = 72,
                                   .height = 12});
    auto const heading_inline_line = std::ranges::find_if(
        heading_inline_transcript, [](std::string const& line) { return strip_sgr(line).find("Why sourceInfo should stay visible.") != std::string::npos; });
    expect(heading_inline_line != heading_inline_transcript.end() && strip_sgr(*heading_inline_line).find("###") == std::string::npos &&
               has_active_sgr_at_text(*heading_inline_line, "sourceInfo", kWarningSgr) &&
               has_active_sgr_at_text(*heading_inline_line, "should stay", kBoldSgr) && has_active_sgr_at_text(*heading_inline_line, "visible", kBoldSgr) &&
               strip_sgr(*heading_inline_line).find("`sourceInfo`") == std::string::npos &&
               strip_sgr(*heading_inline_line).find("**visible**") == std::string::npos,
           "tui assistant renderer preserves heading styling around inline code and bold Markdown spans");

    auto const strikethrough_transcript =
        ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                             .provider = "openai",
                                                             .model = "gpt-5.5",
                                                             .session_id = "session_test",
                                                             .input = "",
                                                             .status = "ready",
                                                             .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Use ~~strike~~ and ~literal~."}},
                                                             .width = 56,
                                                             .height = 12});
    auto const strikethrough_line = std::ranges::find_if(
        strikethrough_transcript, [](std::string const& line) { return strip_sgr(line).find("Use strike and ~literal~.") != std::string::npos; });
    expect(strikethrough_line != strikethrough_transcript.end() && has_active_sgr_at_text(*strikethrough_line, "strike", kStrikethroughSgr) &&
               !has_active_sgr_at_text(*strikethrough_line, "literal", kStrikethroughSgr) &&
               strip_sgr(*strikethrough_line).find("~~strike~~") == std::string::npos,
           "tui assistant renderer emits Pi-style strikethrough SGR for double-tilde Markdown while preserving single-tilde text");

    auto const link_fallback_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
        .mode = "build",
        .provider = "openai",
        .model = "gpt-5.5",
        .session_id = "session_test",
        .input = "",
        .status = "ready",
        .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                                .text = "[https://example.test](https://example.test) [user@example.test](mailto:user@example.test) "
                                                        "[docs](https://example.test/docs)"}},
        .width = 120,
        .height = 12});
    auto link_fallback_plain = std::string{};
    for (auto const& line : link_fallback_transcript)
    {
      link_fallback_plain += strip_sgr(line);
      link_fallback_plain.push_back(' ');
    }
    expect(link_fallback_plain.find("https://example.test") != std::string::npos &&
               link_fallback_plain.find("https://example.test (https://example.test)") == std::string::npos &&
               link_fallback_plain.find("mailto:user@example.test") == std::string::npos &&
               link_fallback_plain.find("user@example.test") != std::string::npos &&
               link_fallback_plain.find("docs (https://example.test/docs)") != std::string::npos,
           "tui assistant renderer avoids duplicate URL/mailto fallback when Markdown link labels already equal targets");
  }

  {
    ScopedEnvVar no_color_hyperlink_guard("NO_COLOR", "");
    ScopedTerminalCapabilityProfile hyperlink_terminal("vscode");
    auto const osc8_link_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
        .mode = "build",
        .provider = "openai",
        .model = "gpt-5.5",
        .session_id = "session_test",
        .input = "",
        .status = "ready",
        .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "[docs](https://example.test/docs) and https://example.test/bare"}},
        .width = 120,
        .height = 12});
    auto osc8_joined = std::string{};
    for (auto const& line : osc8_link_transcript) osc8_joined += line;
    expect(osc8_joined.find("\x1b]8;;https://example.test/docs\x1b\\") != std::string::npos &&
               osc8_joined.find("\x1b]8;;https://example.test/bare\x1b\\") != std::string::npos && osc8_joined.find("\x1b]8;;\x1b\\") != std::string::npos &&
               osc8_joined.find("(https://example.test/docs)") == std::string::npos,
           "tui assistant renderer emits Pi-style OSC 8 hyperlinks when terminal capabilities advertise them");
  }

  {
    ScopedEnvVar no_color_hyperlink_guard("NO_COLOR", "1");
    ScopedTerminalCapabilityProfile hyperlink_terminal("vscode");
    auto const plain_link_transcript = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "[docs](https://example.test/docs)"}},
                                   .width = 120,
                                   .height = 12});
    auto plain_link_joined = std::string{};
    for (auto const& line : plain_link_transcript) plain_link_joined += line;
    expect(plain_link_joined.find("\x1b]8;;") == std::string::npos && plain_link_joined.find("docs (https://example.test/docs)") != std::string::npos,
           "plain TUI output suppresses OSC 8 hyperlinks and keeps visible link fallback text");
  }

  {
    ScopedTerminalCapabilityProfile no_hyperlink_terminal("");
    auto const bare_url_transcript = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Visit https://example.test/docs."}},
                                   .width = 72,
                                   .height = 12});
    auto const bare_url_line = std::ranges::find_if(
        bare_url_transcript, [](std::string const& line) { return strip_sgr(line).find("Visit https://example.test/docs.") != std::string::npos; });
    expect(bare_url_line != bare_url_transcript.end() && has_active_sgr_at_text(*bare_url_line, "https://example.test/docs", kUnderlineSgr) &&
               bare_url_line->find(std::string("https://example.test/docs") + std::string(kResetSgr) + ".") != std::string::npos &&
               strip_sgr(*bare_url_line).find("(https://example.test/docs)") == std::string::npos,
           "tui assistant renderer styles bare web URLs once and keeps trailing punctuation outside the link span");

    auto const bare_email_transcript =
        ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                             .provider = "openai",
                                                             .model = "gpt-5.5",
                                                             .session_id = "session_test",
                                                             .input = "",
                                                             .status = "ready",
                                                             .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Contact user@example.test."}},
                                                             .width = 72,
                                                             .height = 12});
    auto const bare_email_line = std::ranges::find_if(
        bare_email_transcript, [](std::string const& line) { return strip_sgr(line).find("Contact user@example.test.") != std::string::npos; });
    expect(bare_email_line != bare_email_transcript.end() && has_active_sgr_at_text(*bare_email_line, "user@example.test", kUnderlineSgr) &&
               bare_email_line->find(std::string("user@example.test") + std::string(kResetSgr) + ".") != std::string::npos &&
               strip_sgr(*bare_email_line).find("mailto:") == std::string::npos && strip_sgr(*bare_email_line).find("(user@example.test)") == std::string::npos,
           "tui assistant renderer styles bare email autolinks once without exposing a mailto fallback");
  }

  auto const wrapped_markdown_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "- alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu\n> quoted alpha beta gamma "
                                                      "delta epsilon zeta eta theta iota kappa"}},
      .width = 48,
      .height = 16});
  auto const bullet_continuation =
      std::ranges::find_if(wrapped_markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("  theta iota") != std::string::npos; });
  auto const quote_continuation =
      std::ranges::find_if(wrapped_markdown_transcript, [](std::string const& line) { return strip_sgr(line).find("│ eta theta") != std::string::npos; });
  auto const bullet_continuation_is_plain =
      bullet_continuation != wrapped_markdown_transcript.end() && !has_active_sgr_at_text(*bullet_continuation, "theta iota", kMutedSgr);
  auto const quote_continuation_is_plain =
      quote_continuation != wrapped_markdown_transcript.end() && !has_active_sgr_at_text(*quote_continuation, "eta theta", kMutedSgr);
  expect(bullet_continuation_is_plain && quote_continuation_is_plain, "tui assistant renderer keeps wrapped list and quote continuations out of code styling");

  auto const list_quote_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "- > alpha beta gamma delta epsilon zeta eta theta iota kappa\n>no-space quote"}},
      .width = 42,
      .height = 16});
  auto const list_quote_first =
      std::ranges::find_if(list_quote_transcript, [](std::string const& line) { return strip_sgr(line).find("- │ alpha beta") != std::string::npos; });
  auto const list_quote_wrap =
      std::ranges::find_if(list_quote_transcript, [](std::string const& line) { return strip_sgr(line).find("  │ zeta eta") != std::string::npos; });
  auto const no_space_quote =
      std::ranges::find_if(list_quote_transcript, [](std::string const& line) { return strip_sgr(line).find("│ no-space quote") != std::string::npos; });
  expect(list_quote_first != list_quote_transcript.end() && list_quote_wrap != list_quote_transcript.end() && no_space_quote != list_quote_transcript.end() &&
             std::ranges::all_of(list_quote_transcript, [](std::string const& line) { return visible_columns(line) <= 42; }),
         "tui assistant renderer uses Pi-style quote borders for plain and list-contained blockquotes");

  auto const lazy_blockquote_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = ">Foo\nbar with **bold** and `code`\n\nagain"}},
                                 .width = 58,
                                 .height = 14});
  auto const lazy_quote_foo =
      std::ranges::find_if(lazy_blockquote_transcript, [](std::string const& line) { return strip_sgr(line).find("│ Foo") != std::string::npos; });
  auto const lazy_quote_bar = std::ranges::find_if(
      lazy_blockquote_transcript, [](std::string const& line) { return strip_sgr(line).find("│ bar with bold and code") != std::string::npos; });
  auto const lazy_quote_after_blank = std::ranges::find_if(lazy_blockquote_transcript, [](std::string const& line) {
    auto const visible = strip_sgr(line);
    return visible.find("again") != std::string::npos && visible.find("│ again") == std::string::npos;
  });
  expect(lazy_quote_foo != lazy_blockquote_transcript.end() && lazy_quote_bar != lazy_blockquote_transcript.end() &&
             lazy_quote_after_blank != lazy_blockquote_transcript.end() && has_active_sgr_at_text(*lazy_quote_bar, "bold", kBoldSgr) &&
             has_active_sgr_at_text(*lazy_quote_bar, "code", kWarningSgr) &&
             std::ranges::all_of(lazy_blockquote_transcript, [](std::string const& line) { return visible_columns(line) <= 58; }),
         "tui assistant renderer applies Pi-style lazy blockquote continuation until a blank line");

  auto const divider_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Before\n---\nMiddle\n* * *\nAfter\n___\nnot --- a rule"}},
                                 .width = 44,
                                 .height = 18});
  auto const divider_lines = std::ranges::count_if(divider_transcript, [](std::string const& line) {
    auto const visible = strip_sgr(line);
    return visible.find("─") != std::string::npos;
  });
  expect(divider_lines == 3 &&
             std::ranges::any_of(divider_transcript, [](std::string const& line) { return strip_sgr(line).find("not --- a rule") != std::string::npos; }) &&
             std::ranges::all_of(divider_transcript, [](std::string const& line) { return visible_columns(line) <= 44; }),
         "tui assistant renderer renders Markdown horizontal rules without treating ordinary dashed text as a divider");

  auto const task_list_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .label = "ava", .text = "- [ ] alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu\n- [x] finished item"}},
                                 .width = 48,
                                 .height = 16});
  auto const task_unchecked =
      std::ranges::find_if(task_list_transcript, [](std::string const& line) { return strip_sgr(line).find("- [ ] alpha beta") != std::string::npos; });
  auto const task_checked =
      std::ranges::find_if(task_list_transcript, [](std::string const& line) { return strip_sgr(line).find("- [x] finished item") != std::string::npos; });
  auto const task_continuation =
      std::ranges::find_if(task_list_transcript, [](std::string const& line) { return strip_sgr(line).find("      eta theta") != std::string::npos; });
  expect(task_unchecked != task_list_transcript.end() && task_checked != task_list_transcript.end() && task_continuation != task_list_transcript.end() &&
             !has_active_sgr_at_text(*task_continuation, "eta theta", kMutedSgr) &&
             std::ranges::all_of(task_list_transcript, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui assistant renderer preserves task-list markers and aligns wrapped task continuations");

  auto const ordered_list_transcript =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "1. alpha\n1. beta\n1. gamma"}},
                                                           .width = 48,
                                                           .height = 14});
  auto ordered_list_text = std::string{};
  for (auto const& line : ordered_list_transcript)
  {
    ordered_list_text += strip_sgr(line);
    ordered_list_text.push_back('\n');
  }
  expect(ordered_list_text.find("1. alpha") != std::string::npos && ordered_list_text.find("2. beta") != std::string::npos &&
             ordered_list_text.find("3. gamma") != std::string::npos && ordered_list_text.find("1. beta") == std::string::npos &&
             std::ranges::all_of(ordered_list_transcript, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui assistant renderer normalizes repeated ordered-list markers Pi-style");

  auto const ordered_code_list_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "1. First item\n\n```typescript\n// code block\n```\n\n1. Second item\n\n```typescript\n// another code "
                                                      "block\n```\n\n1. Third item"}},
      .width = 72,
      .height = 24});
  auto ordered_code_list_text = std::string{};
  for (auto const& line : ordered_code_list_transcript)
  {
    ordered_code_list_text += strip_sgr(line);
    ordered_code_list_text.push_back('\n');
  }
  expect(ordered_code_list_text.find("1. First item") != std::string::npos && ordered_code_list_text.find("2. Second item") != std::string::npos &&
             ordered_code_list_text.find("3. Third item") != std::string::npos && ordered_code_list_text.find("1. Second item") == std::string::npos &&
             ordered_code_list_text.find("``` typescript") != std::string::npos && ordered_code_list_text.find("// another code block") != std::string::npos &&
             std::ranges::all_of(ordered_code_list_transcript, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui assistant renderer keeps ordered-list numbering across unindented fenced code blocks");

  auto assert_code_block_spacing = [](std::string const& markdown) {
    auto const transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                 .provider = "openai",
                                                                                 .model = "gpt-5.5",
                                                                                 .session_id = "session_test",
                                                                                 .input = "",
                                                                                 .status = "ready",
                                                                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = markdown}},
                                                                                 .width = 72,
                                                                                 .height = 18});
    std::vector<std::string> visible;
    visible.reserve(transcript.size());
    for (auto const& line : transcript)
    {
      auto stripped = strip_sgr(line);
      while (!stripped.empty() && stripped.back() == ' ') stripped.pop_back();
      visible.push_back(std::move(stripped));
    }
    auto const paragraph = std::ranges::find(visible, "  hello this is text");
    auto const index = paragraph == visible.end() ? visible.size() : static_cast<std::size_t>(paragraph - visible.begin());
    expect(index + 6 < visible.size() && visible[index + 1].empty() && visible[index + 2] == "  ```" && visible[index + 3] == "    code block" &&
               visible[index + 4] == "  ```" && visible[index + 5].empty() && visible[index + 6] == "  more text",
           "tui assistant renderer normalizes paragraph/fenced-code spacing to one blank line");
  };
  assert_code_block_spacing("hello this is text\n```\ncode block\n```\nmore text");
  assert_code_block_spacing("hello this is text\n\n```\ncode block\n```\n\nmore text");

  auto const heading_spacing_transcript =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "# Hello\nThis is a paragraph"}},
                                                           .width = 72,
                                                           .height = 14});
  std::vector<std::string> heading_visible;
  heading_visible.reserve(heading_spacing_transcript.size());
  for (auto const& line : heading_spacing_transcript)
  {
    auto visible = strip_sgr(line);
    while (!visible.empty() && visible.back() == ' ') visible.pop_back();
    heading_visible.push_back(std::move(visible));
  }
  auto const heading_line = std::ranges::find(heading_visible, "  Hello");
  auto const heading_index = heading_line == heading_visible.end() ? heading_visible.size() : static_cast<std::size_t>(heading_line - heading_visible.begin());
  expect(
      heading_index + 2 < heading_visible.size() && heading_visible[heading_index + 1].empty() && heading_visible[heading_index + 2] == "  This is a paragraph",
      "tui assistant renderer normalizes heading/paragraph spacing to one blank line");

  auto const divider_spacing_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "hello world\n---\nagain, hello world"}},
                                 .width = 72,
                                 .height = 14});
  std::vector<std::string> divider_visible;
  divider_visible.reserve(divider_spacing_transcript.size());
  for (auto const& line : divider_spacing_transcript)
  {
    auto visible = strip_sgr(line);
    while (!visible.empty() && visible.back() == ' ') visible.pop_back();
    divider_visible.push_back(std::move(visible));
  }
  auto const first_divider_line = std::ranges::find(divider_visible, "  hello world");
  auto const divider_index =
      first_divider_line == divider_visible.end() ? divider_visible.size() : static_cast<std::size_t>(first_divider_line - divider_visible.begin());
  expect(divider_index + 3 < divider_visible.size() && divider_visible[divider_index + 1].find("─") != std::string::npos &&
             divider_visible[divider_index + 2].empty() && divider_visible[divider_index + 3] == "  again, hello world",
         "tui assistant renderer normalizes divider/paragraph spacing to one blank line");

  auto const html_literal_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "This is text with <thinking>hidden content</thinking> that should stay visible.\n```html\n<div>Some "
                                                      "HTML</div>\n```"}},
      .width = 88,
      .height = 18});
  auto html_literal_text = std::string{};
  for (auto const& line : html_literal_transcript)
  {
    html_literal_text += strip_sgr(line);
    html_literal_text.push_back('\n');
  }
  expect(
      html_literal_text.find("<thinking>hidden content</thinking>") != std::string::npos && html_literal_text.find("<div>Some HTML</div>") != std::string::npos,
      "tui assistant renderer keeps HTML-like tags visible in prose and fenced code");

  auto const loose_list_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "1. Lorem ipsum dolor sit amet.\n\n   Ut enim ad minim veniam.\n\n1. Duis aute irure dolor.\n\n   "
                                                      "Excepteur sint occaecat cupidatat.\n\n1. Beep boop"}},
      .width = 88,
      .height = 24});
  std::vector<std::string> loose_list_visible;
  loose_list_visible.reserve(loose_list_transcript.size());
  for (auto const& line : loose_list_transcript)
  {
    auto visible = strip_sgr(line);
    while (!visible.empty() && visible.back() == ' ') visible.pop_back();
    loose_list_visible.push_back(std::move(visible));
  }
  auto const first_loose_item = std::ranges::find(loose_list_visible, "  1. Lorem ipsum dolor sit amet.");
  auto const loose_index =
      first_loose_item == loose_list_visible.end() ? loose_list_visible.size() : static_cast<std::size_t>(first_loose_item - loose_list_visible.begin());
  expect(loose_index + 8 < loose_list_visible.size() && loose_list_visible[loose_index + 1].empty() &&
             loose_list_visible[loose_index + 2] == "     Ut enim ad minim veniam." && loose_list_visible[loose_index + 3].empty() &&
             loose_list_visible[loose_index + 4] == "  2. Duis aute irure dolor." && loose_list_visible[loose_index + 5].empty() &&
             loose_list_visible[loose_index + 6] == "     Excepteur sint occaecat cupidatat." && loose_list_visible[loose_index + 7].empty() &&
             loose_list_visible[loose_index + 8] == "  3. Beep boop",
         "tui assistant renderer preserves Pi-style loose ordered-list continuations and numbering");

  auto const nested_list_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "- parent\n  - alpha beta gamma delta epsilon zeta eta theta iota\n    + grand child\n1. ordered\n   "
                                                      "1) nested ordered"}},
      .width = 48,
      .height = 18});
  auto const nested_unordered =
      std::ranges::find_if(nested_list_transcript, [](std::string const& line) { return strip_sgr(line).find("    - alpha beta") != std::string::npos; });
  auto const nested_continuation =
      std::ranges::find_if(nested_list_transcript, [](std::string const& line) { return strip_sgr(line).find("      eta theta") != std::string::npos; });
  auto const nested_plus =
      std::ranges::find_if(nested_list_transcript, [](std::string const& line) { return strip_sgr(line).find("        + grand child") != std::string::npos; });
  auto const nested_ordered =
      std::ranges::find_if(nested_list_transcript, [](std::string const& line) { return strip_sgr(line).find("    1) nested ordered") != std::string::npos; });
  expect(nested_unordered != nested_list_transcript.end() && nested_continuation != nested_list_transcript.end() &&
             nested_plus != nested_list_transcript.end() && nested_ordered != nested_list_transcript.end() &&
             !has_active_sgr_at_text(*nested_continuation, "eta theta", kMutedSgr) &&
             std::ranges::all_of(nested_list_transcript, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui assistant renderer preserves Pi-style nested list indentation and wrapped continuations");

  auto const table_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "| Name | Age |\n| --- | ---: |\n| Alice | 30 |\n| Bob | 25 |"}},
      .width = 64,
      .height = 16});
  auto const table_header = std::ranges::find_if(table_transcript, [](std::string const& line) {
    auto const visible = strip_sgr(line);
    return visible.find("│ Name") != std::string::npos && visible.find("Age │") != std::string::npos;
  });
  auto const table_alice = std::ranges::find_if(table_transcript, [](std::string const& line) {
    auto const visible = strip_sgr(line);
    return visible.find("│ Alice") != std::string::npos && visible.find(" 30 │") != std::string::npos;
  });
  auto const table_top_border = std::ranges::find_if(table_transcript, [](std::string const& line) {
    auto const visible = strip_sgr(line);
    return visible.find("┌") != std::string::npos && visible.find("┬") != std::string::npos && visible.find("┐") != std::string::npos;
  });
  auto const table_bottom_border = std::ranges::find_if(table_transcript, [](std::string const& line) {
    auto const visible = strip_sgr(line);
    return visible.find("└") != std::string::npos && visible.find("┴") != std::string::npos && visible.find("┘") != std::string::npos;
  });
  auto const table_dividers = std::ranges::count_if(table_transcript, [](std::string const& line) { return strip_sgr(line).find("┼") != std::string::npos; });
  expect(table_header != table_transcript.end() && table_alice != table_transcript.end() && table_top_border != table_transcript.end() &&
             table_bottom_border != table_transcript.end() && table_dividers == 2 &&
             std::ranges::any_of(table_transcript, [](std::string const& line) { return strip_sgr(line).find("─") != std::string::npos; }) &&
             std::ranges::all_of(table_transcript, [](std::string const& line) { return visible_columns(line) <= 64; }),
         "tui assistant renderer renders simple Markdown tables with Pi-style outer borders, dividers, and right alignment");

  auto const too_narrow_table_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "| A | B | C | D | E |\n| --- | --- | --- | --- | --- |\n| 1 | 2 | 3 | 4 | 5 |"}},
      .width = 20,
      .height = 16});
  auto const too_narrow_text =
      std::accumulate(too_narrow_table_transcript.begin(), too_narrow_table_transcript.end(), std::string{}, [](std::string acc, std::string const& line) {
        acc += " ";
        acc += strip_sgr(line);
        return acc;
      });
  expect(too_narrow_text.find("| A | B | C") != std::string::npos && too_narrow_text.find("| 1 | 2 | 3") != std::string::npos &&
             std::ranges::none_of(too_narrow_table_transcript,
                                  [](std::string const& line) {
                                    auto const visible = strip_sgr(line);
                                    auto const quiet_composer_row = visible.starts_with("│  Type a message...") || visible.starts_with("│  GPT-");
                                    return !quiet_composer_row && (visible.find("┌") != std::string::npos || visible.find("│") != std::string::npos ||
                                                                   visible.find("└") != std::string::npos);
                                  }) &&
             std::ranges::all_of(too_narrow_table_transcript, [](std::string const& line) { return visible_columns(line) <= 20; }),
         "tui assistant renderer falls back to wrapped raw Markdown when a table is too narrow for stable borders");

  auto const narrow_table_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .label = "ava", .text = "| Command | Description |\n| --- | --- |\n| npm install | Install all dependencies for the workspace |"}},
      .width = 40,
      .height = 18});
  auto const narrow_table_text =
      std::accumulate(narrow_table_transcript.begin(), narrow_table_transcript.end(), std::string{}, [](std::string acc, std::string const& line) {
        acc += " ";
        acc += strip_sgr(line);
        return acc;
      });
  expect(narrow_table_text.find("Command") != std::string::npos && narrow_table_text.find("npm install") != std::string::npos &&
             narrow_table_text.find("dependencies") != std::string::npos && narrow_table_text.find("workspace") != std::string::npos &&
             std::ranges::any_of(narrow_table_transcript, [](std::string const& line) { return strip_sgr(line).find("│ Command") != std::string::npos; }) &&
             std::ranges::all_of(narrow_table_transcript, [](std::string const& line) { return visible_columns(line) <= 40; }),
         "tui assistant renderer wraps Markdown table cells while preserving borders and content");

  auto const one_column_table_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                              .text = "| Header |\n| --- |\n| This is a very long cell content that should wrap |\n| "
                                                      "prefix https://example.com/this/is/a/very/long/url/that/should/wrap |"}},
      .width = 36,
      .height = 24});
  auto count_table_borders = [](std::string const& line) {
    auto const visible = strip_sgr(line);
    auto count = std::size_t{0};
    for (auto found = visible.find("│"); found != std::string::npos; found = visible.find("│", found + 1))
    {
      ++count;
    }
    return count;
  };
  auto one_column_table_text = std::string{};
  for (auto const& line : one_column_table_transcript)
  {
    auto const visible = strip_sgr(line);
    for (std::size_t index = 0; index < visible.size();)
    {
      if (visible.compare(index, std::string_view("│").size(), "│") == 0 || visible.compare(index, std::string_view("├").size(), "├") == 0 ||
          visible.compare(index, std::string_view("┤").size(), "┤") == 0 || visible.compare(index, std::string_view("┼").size(), "┼") == 0 ||
          visible.compare(index, std::string_view("─").size(), "─") == 0)
      {
        index += std::string_view("│").size();
        continue;
      }
      if (visible[index] != ' ')
        one_column_table_text.push_back(visible[index]);
      ++index;
    }
  }
  expect(std::ranges::any_of(one_column_table_transcript, [](std::string const& line) { return strip_sgr(line).find("│ Header") != std::string::npos; }) &&
             one_column_table_text.find("verylongcellcontent") != std::string::npos &&
             one_column_table_text.find("prefixhttps://example.com/this/is/a/very/long/url/that/should/wrap") != std::string::npos &&
             std::ranges::all_of(one_column_table_transcript, [](std::string const& line) { return visible_columns(line) <= 36; }) &&
             std::ranges::all_of(one_column_table_transcript,
                                 [&](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("│") == std::string::npos || visible.starts_with("│  Type a message...") ||
                                          visible.starts_with("│  GPT-") || count_table_borders(line) == 2;
                                 }),
         "tui assistant renderer supports one-column tables and wraps long unbroken table tokens without losing borders");

  auto const styled_table_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "| Code |\n| --- |\n| `averyveryveryverylongidentifier` |"}},
                                 .width = 36,
                                 .height = 18});
  auto styled_table_text = std::string{};
  for (auto const& line : styled_table_transcript)
  {
    auto const visible = strip_sgr(line);
    for (std::size_t index = 0; index < visible.size();)
    {
      if (visible.compare(index, std::string_view("│").size(), "│") == 0 || visible.compare(index, std::string_view("├").size(), "├") == 0 ||
          visible.compare(index, std::string_view("┤").size(), "┤") == 0 || visible.compare(index, std::string_view("┼").size(), "┼") == 0 ||
          visible.compare(index, std::string_view("─").size(), "─") == 0)
      {
        index += std::string_view("│").size();
        continue;
      }
      if (visible[index] != ' ')
        styled_table_text.push_back(visible[index]);
      ++index;
    }
  }
  expect(std::ranges::any_of(styled_table_transcript, [&kWarningSgr](std::string const& line) { return has_active_sgr_at_text(line, "avery", kWarningSgr); }) &&
             styled_table_text.find("averyveryveryverylongidentifier") != std::string::npos && styled_table_text.find("`") == std::string::npos &&
             std::ranges::all_of(styled_table_transcript, [](std::string const& line) { return visible_columns(line) <= 36; }) &&
             std::ranges::all_of(styled_table_transcript,
                                 [&](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("│") == std::string::npos || visible.starts_with("│  Type a message...") ||
                                          visible.starts_with("│  GPT-") || count_table_borders(line) == 2;
                                 }),
         "tui assistant renderer wraps styled inline code inside table cells without breaking borders");

  auto const narrow_three_column_table = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "| A | B | C |\n| --- | --- | --- |\n| 1 | 2 | 3 |"}},
                                 .width = 22,
                                 .height = 14});
  auto narrow_three_column_text = std::string{};
  for (auto const& line : narrow_three_column_table)
  {
    narrow_three_column_text += strip_sgr(line);
    narrow_three_column_text.push_back('\n');
  }
  expect(!narrow_three_column_table.empty() && narrow_three_column_text.find("A") != std::string::npos &&
             narrow_three_column_text.find("B") != std::string::npos && narrow_three_column_text.find("C") != std::string::npos &&
             narrow_three_column_text.find("1") != std::string::npos && narrow_three_column_text.find("2") != std::string::npos &&
             narrow_three_column_text.find("3") != std::string::npos &&
             std::ranges::any_of(narrow_three_column_table, [](std::string const& line) { return strip_sgr(line).find("│ A") != std::string::npos; }) &&
             std::ranges::all_of(narrow_three_column_table, [](std::string const& line) { return visible_columns(line) <= 22; }),
         "tui assistant renderer keeps narrow multi-column Markdown tables bounded and readable");

  auto const highlighted_code_transcript =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                                                                                   .text = "```cpp\n"
                                                                                                           "int main() {\n"
                                                                                                           "  std::string value = \"ok\";\n"
                                                                                                           "  return 42;\n"
                                                                                                           "}\n"
                                                                                                           "```\n"
                                                                                                           "```diff\n"
                                                                                                           "@@ -1 +1 @@\n"
                                                                                                           "-old\n"
                                                                                                           "+new\n"
                                                                                                           " context\n"
                                                                                                           "```\n"
                                                                                                           "```json\n"
                                                                                                           "{\"ok\": true, \"n\": 12}\n"
                                                                                                           "```\n"
                                                                                                           "```sh\n"
                                                                                                           "echo \"ok\" # comment\n"
                                                                                                           "```\n"
                                                                                                           "```typescript\n"
                                                                                                           "interface User { name: string; }\n"
                                                                                                           "const greet = (user: User): string => \"hi\";\n"
                                                                                                           "```\n"
                                                                                                           "```javascript\n"
                                                                                                           "async function load() {\n"
                                                                                                           "  return await fetch(\"/ok\");\n"
                                                                                                           "}\n"
                                                                                                           "```\n"
                                                                                                           "```html\n"
                                                                                                           "<div class=\"ok\">Hi</div>\n"
                                                                                                           "<!-- note -->\n"
                                                                                                           "```\n"
                                                                                                           "```css\n"
                                                                                                           ".card { color: #fff; margin: 12px; }\n"
                                                                                                           "```\n"
                                                                                                           "```yaml\n"
                                                                                                           "enabled: true\n"
                                                                                                           "name: \"ava\"\n"
                                                                                                           "count: 42\n"
                                                                                                           "# comment\n"
                                                                                                           "```\n"
                                                                                                           "```cmake\n"
                                                                                                           "add_executable(ava main.cpp)\n"
                                                                                                           "set(AVA_ENABLED ON)\n"
                                                                                                           "# build comment\n"
                                                                                                           "```\n"
                                                                                                           "```toml\n"
                                                                                                           "[tool.ava]\n"
                                                                                                           "name = \"ava\"\n"
                                                                                                           "enabled = true\n"
                                                                                                           "count = 42\n"
                                                                                                           "# note\n"
                                                                                                           "```\n"
                                                                                                           "```ini\n"
                                                                                                           "[server]\n"
                                                                                                           "port = 8080\n"
                                                                                                           "enabled = yes\n"
                                                                                                           "; note\n"
                                                                                                           "```\n"
                                                                                                           "```text\n"
                                                                                                           "return \"plain\";\n"
                                                                                                           "```"}},
                                                           .width = 72,
                                                           .height = 120});
  auto const cpp_signature =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("int main() {") != std::string::npos; });
  auto const cpp_string = std::ranges::find_if(
      highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("std::string value = \"ok\";") != std::string::npos; });
  auto const cpp_return =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("return 42;") != std::string::npos; });
  auto const diff_hunk =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("@@ -1 +1 @@") != std::string::npos; });
  auto const diff_removed =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("-old") != std::string::npos; });
  auto const diff_added =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("+new") != std::string::npos; });
  auto const json_code = std::ranges::find_if(highlighted_code_transcript,
                                              [](std::string const& line) { return strip_sgr(line).find("{\"ok\": true, \"n\": 12}") != std::string::npos; });
  auto const shell_code = std::ranges::find_if(highlighted_code_transcript,
                                               [](std::string const& line) { return strip_sgr(line).find("echo \"ok\" # comment") != std::string::npos; });
  auto const typescript_interface = std::ranges::find_if(
      highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("interface User { name: string; }") != std::string::npos; });
  auto const typescript_const = std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) {
    return strip_sgr(line).find("const greet = (user: User): string => \"hi\";") != std::string::npos;
  });
  auto const javascript_async = std::ranges::find_if(
      highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("async function load() {") != std::string::npos; });
  auto const javascript_return = std::ranges::find_if(
      highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("return await fetch(\"/ok\");") != std::string::npos; });
  auto const html_tag = std::ranges::find_if(highlighted_code_transcript,
                                             [](std::string const& line) { return strip_sgr(line).find("<div class=\"ok\">Hi</div>") != std::string::npos; });
  auto const html_comment =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("<!-- note -->") != std::string::npos; });
  auto const css_code = std::ranges::find_if(
      highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find(".card { color: #fff; margin: 12px; }") != std::string::npos; });
  auto const yaml_enabled =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("enabled: true") != std::string::npos; });
  auto const yaml_name =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("name: \"ava\"") != std::string::npos; });
  auto const yaml_count =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("count: 42") != std::string::npos; });
  auto const yaml_comment =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("# comment") != std::string::npos; });
  auto const cmake_command = std::ranges::find_if(
      highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("add_executable(ava main.cpp)") != std::string::npos; });
  auto const cmake_option = std::ranges::find_if(highlighted_code_transcript,
                                                 [](std::string const& line) { return strip_sgr(line).find("set(AVA_ENABLED ON)") != std::string::npos; });
  auto const cmake_comment =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("# build comment") != std::string::npos; });
  auto const toml_header =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("[tool.ava]") != std::string::npos; });
  auto const toml_name =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("name = \"ava\"") != std::string::npos; });
  auto const toml_enabled =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("enabled = true") != std::string::npos; });
  auto const toml_count =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("count = 42") != std::string::npos; });
  auto const toml_comment =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("# note") != std::string::npos; });
  auto const ini_header =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("[server]") != std::string::npos; });
  auto const ini_port =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("port = 8080") != std::string::npos; });
  auto const ini_enabled =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("enabled = yes") != std::string::npos; });
  auto const ini_comment =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("; note") != std::string::npos; });
  auto const fallback_code =
      std::ranges::find_if(highlighted_code_transcript, [](std::string const& line) { return strip_sgr(line).find("return \"plain\";") != std::string::npos; });
  auto const found_highlight_examples =
      cpp_signature != highlighted_code_transcript.end() && cpp_string != highlighted_code_transcript.end() &&
      cpp_return != highlighted_code_transcript.end() && diff_hunk != highlighted_code_transcript.end() && diff_removed != highlighted_code_transcript.end() &&
      diff_added != highlighted_code_transcript.end() && json_code != highlighted_code_transcript.end() && shell_code != highlighted_code_transcript.end() &&
      typescript_interface != highlighted_code_transcript.end() && typescript_const != highlighted_code_transcript.end() &&
      javascript_async != highlighted_code_transcript.end() && javascript_return != highlighted_code_transcript.end() &&
      html_tag != highlighted_code_transcript.end() && html_comment != highlighted_code_transcript.end() && css_code != highlighted_code_transcript.end() &&
      yaml_enabled != highlighted_code_transcript.end() && yaml_name != highlighted_code_transcript.end() && yaml_count != highlighted_code_transcript.end() &&
      yaml_comment != highlighted_code_transcript.end() && cmake_command != highlighted_code_transcript.end() &&
      cmake_option != highlighted_code_transcript.end() && cmake_comment != highlighted_code_transcript.end() &&
      toml_header != highlighted_code_transcript.end() && toml_name != highlighted_code_transcript.end() && toml_enabled != highlighted_code_transcript.end() &&
      toml_count != highlighted_code_transcript.end() && toml_comment != highlighted_code_transcript.end() && ini_header != highlighted_code_transcript.end() &&
      ini_port != highlighted_code_transcript.end() && ini_enabled != highlighted_code_transcript.end() && ini_comment != highlighted_code_transcript.end() &&
      fallback_code != highlighted_code_transcript.end();
  expect(found_highlight_examples, "tui assistant renderer keeps all fenced-code highlight examples visible");
  if (found_highlight_examples)
  {
    expect(has_active_sgr_at_text(*cpp_signature, "int", kWarningSgr) && has_active_sgr_at_text(*cpp_signature, "main", kAccentSgr) &&
               has_active_sgr_at_text(*cpp_string, "\"ok\"", kSuccessSgr) && has_active_sgr_at_text(*cpp_return, "return", kWarningSgr) &&
               has_active_sgr_at_text(*cpp_return, "42", kAccentSgr),
           "tui assistant renderer applies lightweight C++ syntax coloring");
    expect(has_active_sgr_at_text(*diff_hunk, "@@ -1 +1 @@", kAccentSgr) && has_active_sgr_at_text(*diff_removed, "-old", kErrorSgr) &&
               has_active_sgr_at_text(*diff_added, "+new", kSuccessSgr),
           "tui assistant renderer applies diff syntax coloring");
    expect(has_active_sgr_at_text(*json_code, "\"ok\"", kSuccessSgr) && has_active_sgr_at_text(*json_code, "true", kWarningSgr) &&
               has_active_sgr_at_text(*json_code, "12", kAccentSgr),
           "tui assistant renderer applies JSON syntax coloring");
    expect(has_active_sgr_at_text(*shell_code, "\"ok\"", kSuccessSgr) && has_active_sgr_at_text(*shell_code, "# comment", kMutedSgr),
           "tui assistant renderer applies shell syntax coloring");
    expect(has_active_sgr_at_text(*typescript_interface, "interface", kWarningSgr) && has_active_sgr_at_text(*typescript_interface, "string", kWarningSgr) &&
               has_active_sgr_at_text(*typescript_const, "const", kWarningSgr) && has_active_sgr_at_text(*typescript_const, "\"hi\"", kSuccessSgr),
           "tui assistant renderer applies TypeScript syntax coloring for Pi-style language names");
    expect(has_active_sgr_at_text(*javascript_async, "async", kWarningSgr) && has_active_sgr_at_text(*javascript_async, "function", kWarningSgr) &&
               has_active_sgr_at_text(*javascript_async, "load", kAccentSgr) && has_active_sgr_at_text(*javascript_return, "return", kWarningSgr) &&
               has_active_sgr_at_text(*javascript_return, "await", kWarningSgr) && has_active_sgr_at_text(*javascript_return, "fetch", kAccentSgr) &&
               has_active_sgr_at_text(*javascript_return, "\"/ok\"", kSuccessSgr),
           "tui assistant renderer applies JavaScript syntax coloring for Pi-style language names");
    expect(has_active_sgr_at_text(*html_tag, "div", kAccentSgr) && has_active_sgr_at_text(*html_tag, "class", kWarningSgr) &&
               has_active_sgr_at_text(*html_tag, "\"ok\"", kSuccessSgr) && has_active_sgr_at_text(*html_comment, "<!-- note -->", kMutedSgr),
           "tui assistant renderer applies HTML syntax coloring");
    expect(has_active_sgr_at_text(*css_code, "color", kWarningSgr) && has_active_sgr_at_text(*css_code, "#fff", kAccentSgr) &&
               has_active_sgr_at_text(*css_code, "margin", kWarningSgr) && has_active_sgr_at_text(*css_code, "12px", kAccentSgr),
           "tui assistant renderer applies CSS syntax coloring");
    expect(has_active_sgr_at_text(*yaml_enabled, "enabled", kWarningSgr), "tui assistant renderer highlights YAML keys");
    expect(has_active_sgr_at_text(*yaml_enabled, "true", kWarningSgr), "tui assistant renderer highlights YAML booleans");
    expect(has_active_sgr_at_text(*yaml_name, "name", kWarningSgr) && has_active_sgr_at_text(*yaml_name, "\"ava\"", kSuccessSgr),
           "tui assistant renderer highlights YAML string values");
    expect(has_active_sgr_at_text(*yaml_count, "count", kWarningSgr), "tui assistant renderer highlights YAML numeric keys");
    expect(has_active_sgr_at_text(*yaml_count, "42", kAccentSgr), "tui assistant renderer highlights YAML numeric scalar values");
    expect(has_active_sgr_at_text(*yaml_comment, "# comment", kMutedSgr), "tui assistant renderer highlights YAML comments");
    expect(has_active_sgr_at_text(*cmake_command, "add_executable", kAccentSgr) && has_active_sgr_at_text(*cmake_option, "set", kAccentSgr) &&
               has_active_sgr_at_text(*cmake_option, "ON", kWarningSgr) && has_active_sgr_at_text(*cmake_comment, "# build comment", kMutedSgr),
           "tui assistant renderer highlights CMake commands, scalars, and comments");
    expect(has_active_sgr_at_text(*toml_header, "tool.ava", kAccentSgr) && has_active_sgr_at_text(*toml_name, "name", kWarningSgr) &&
               has_active_sgr_at_text(*toml_name, "\"ava\"", kSuccessSgr) && has_active_sgr_at_text(*toml_enabled, "enabled", kWarningSgr) &&
               has_active_sgr_at_text(*toml_enabled, "true", kWarningSgr) && has_active_sgr_at_text(*toml_count, "42", kAccentSgr) &&
               has_active_sgr_at_text(*toml_comment, "# note", kMutedSgr),
           "tui assistant renderer highlights TOML table headers, keys, scalars, and comments");
    expect(has_active_sgr_at_text(*ini_header, "server", kAccentSgr) && has_active_sgr_at_text(*ini_port, "port", kWarningSgr) &&
               has_active_sgr_at_text(*ini_port, "8080", kAccentSgr) && has_active_sgr_at_text(*ini_enabled, "enabled", kWarningSgr) &&
               has_active_sgr_at_text(*ini_enabled, "yes", kWarningSgr) && has_active_sgr_at_text(*ini_comment, "; note", kMutedSgr),
           "tui assistant renderer highlights INI sections, keys, scalars, and comments");
    expect(has_active_sgr_at_text(*fallback_code, "return", kMutedSgr) && !has_active_sgr_at_text(*fallback_code, "return", kWarningSgr),
           "tui assistant renderer preserves dim plain fallback for unsupported code fences");
  }
  expect(std::ranges::all_of(highlighted_code_transcript, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui assistant renderer keeps highlighted code lines width-safe");

  auto const wrapped_code_fence_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{
                                     .label = "ava", .text = std::string("```text\n") + std::string(42, 'a') + "```literal\nomega\n```\nAfter **bold**"}},
                                 .width = 48,
                                 .height = 16});
  auto const code_after_wrapped_ticks =
      std::ranges::find_if(wrapped_code_fence_transcript, [](std::string const& line) { return strip_sgr(line).find("  omega") != std::string::npos; });
  auto const text_after_code =
      std::ranges::find_if(wrapped_code_fence_transcript, [](std::string const& line) { return strip_sgr(line).find("After bold") != std::string::npos; });
  expect(code_after_wrapped_ticks != wrapped_code_fence_transcript.end() && has_active_sgr_at_text(*code_after_wrapped_ticks, "omega", kMutedSgr) &&
             text_after_code != wrapped_code_fence_transcript.end() && !has_active_sgr_at_text(*text_after_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_code, "bold", kBoldSgr),
         "tui assistant renderer keeps wrapped code content beginning with backticks inside the code block");

  auto const indented_fence_content_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "```text\n  ```literal\nomega\n```\nAfter **bold**"}},
                                 .width = 56,
                                 .height = 16});
  auto const indented_ticks =
      std::ranges::find_if(indented_fence_content_transcript, [](std::string const& line) { return strip_sgr(line).find("```literal") != std::string::npos; });
  auto const code_after_indented_ticks =
      std::ranges::find_if(indented_fence_content_transcript, [](std::string const& line) { return strip_sgr(line).find("  omega") != std::string::npos; });
  auto const text_after_indented_code =
      std::ranges::find_if(indented_fence_content_transcript, [](std::string const& line) { return strip_sgr(line).find("After bold") != std::string::npos; });
  expect(indented_ticks != indented_fence_content_transcript.end() && strip_sgr(*indented_ticks).find("``` literal") == std::string::npos &&
             code_after_indented_ticks != indented_fence_content_transcript.end() && has_active_sgr_at_text(*code_after_indented_ticks, "omega", kMutedSgr) &&
             text_after_indented_code != indented_fence_content_transcript.end() && !has_active_sgr_at_text(*text_after_indented_code, "After", kMutedSgr) &&
             has_active_sgr_at_text(*text_after_indented_code, "bold", kBoldSgr),
         "tui assistant renderer keeps indented backtick content inside fenced code blocks");

  auto const narrow_code_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "Intro `ok` and **bold**.\n```text\nvalue `x` and **y**\n```\nDone **bold**."}},
      .width = 30,
      .height = 18});
  auto const narrow_inline =
      std::ranges::find_if(narrow_code_transcript, [](std::string const& line) { return strip_sgr(line).find("Intro ok and bold.") != std::string::npos; });
  auto const narrow_code =
      std::ranges::find_if(narrow_code_transcript, [](std::string const& line) { return strip_sgr(line).find("value `x` and **y**") != std::string::npos; });
  auto const narrow_after_code =
      std::ranges::find_if(narrow_code_transcript, [](std::string const& line) { return strip_sgr(line).find("Done bold.") != std::string::npos; });
  expect(narrow_inline != narrow_code_transcript.end() && has_active_sgr_at_text(*narrow_inline, "ok", kWarningSgr) &&
             has_active_sgr_at_text(*narrow_inline, "bold", kBoldSgr) && narrow_code != narrow_code_transcript.end() &&
             !has_active_sgr_at_text(*narrow_code, "x", kWarningSgr) && !has_active_sgr_at_text(*narrow_code, "y", kBoldSgr) &&
             narrow_after_code != narrow_code_transcript.end() && has_active_sgr_at_text(*narrow_after_code, "bold", kBoldSgr),
         "tui narrow assistant renderer keeps code literal while formatting inline markdown outside code");

  auto const narrow_transcript =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(64, 'x') + " done"}},
                                                           .width = 20,
                                                           .height = 14});
  expect(std::ranges::any_of(narrow_transcript, [](std::string const& line) { return strip_sgr(line).find("xxx") != std::string::npos; }) &&
             std::ranges::all_of(narrow_transcript, [](std::string const& line) { return visible_columns(line) <= 20; }),
         "tui assistant renderer keeps long words readable at narrow widths");

  auto const rows_transcript = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "error", .text = "bad command"}, ava::tui::TranscriptItem{.label = "command", .text = "/help"}},
      .width = 50,
      .height = 10});
  expect(std::ranges::any_of(rows_transcript, [](std::string const& line) { return strip_sgr(line).find("! bad command") != std::string::npos; }) &&
             std::ranges::any_of(rows_transcript, [](std::string const& line) { return strip_sgr(line).find("· /help") != std::string::npos; }),
         "tui keeps errors and generic command rows distinct from message blocks");
  auto const onboarding_transcript =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "setup",
                                                                                                   .text = "Provider auth is not configured for `openai`.\n"
                                                                                                           "Connect with /connect or /login in this TUI.\n"
                                                                                                           "Auth file: /tmp/ava/auth.json"}},
                                                           .width = 72,
                                                           .height = 12});
  expect(std::ranges::any_of(onboarding_transcript,
                             [](std::string const& line) { return strip_sgr(line).find("Provider auth is not configured") != std::string::npos; }) &&
             std::ranges::any_of(onboarding_transcript,
                                 [](std::string const& line) { return strip_sgr(line).find("Connect with /connect or /login") != std::string::npos; }) &&
             std::ranges::all_of(onboarding_transcript, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui renders first-run setup transcript guidance without width overflow");
  auto const compact_status = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "Enter submits. Shift/Ctrl+Enter inserts newline. Alt+Enter queues follow-up. / opens commands.",
                                 .transcript = {},
                                 .width = 120,
                                 .height = 8});
  expect(std::ranges::none_of(compact_status, [](std::string const& line) { return strip_sgr(line).find("Alt+Enter queues follow-up") != std::string::npos; }),
         "tui keeps the composer status compact instead of rendering verbose help");
  auto const status_alert = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "invalid_argument: conflicting TUI keybinding\n  key: Ctrl+P\n  action: model_cycle_forward\x1b[31m",
                                 .transcript = {},
                                 .width = 96,
                                 .height = 10});
  expect(
      std::ranges::any_of(
          status_alert, [](std::string const& line) { return strip_sgr(line).find("! invalid_argument: conflicting TUI keybinding") != std::string::npos; }) &&
          std::ranges::any_of(status_alert, [](std::string const& line) { return strip_sgr(line).find("key: Ctrl+P") != std::string::npos; }) &&
          std::ranges::none_of(status_alert, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
          std::ranges::all_of(status_alert, [](std::string const& line) { return visible_columns(line) <= 96; }),
      "tui renders error-category status strings as compact sanitized alerts above the composer");

  auto const minimum_width = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "hello",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .width = 1,
                                                                                  .height = 1});
  expect(std::ranges::all_of(minimum_width, [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 20; }) &&
             std::ranges::any_of(minimum_width, [](std::string const& line) { return strip_sgr(line).find("│  hello") != std::string::npos; }),
         "tui clamps normal composer rendering to the minimum viewport");

  std::vector<ava::tui::SlashCommandItem> const slash_commands = {
      ava::tui::SlashCommandItem{.command = "/help", .description = "Show help", .category = "General"},
      ava::tui::SlashCommandItem{.command = "/grep", .description = "Search files", .hint = "<text> [glob]", .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/glob", .description = "List matching files", .hint = "<pattern>", .category = "Files"},
      ava::tui::SlashCommandItem{.command = "/quit", .description = "Exit", .category = "General"}};
  auto const grep_commands = ava::tui::filter_slash_commands("/gr", slash_commands);
  expect(grep_commands.size() == 1 && grep_commands.front().command == "/grep", "tui slash palette filters commands by typed prefix");
  auto const fuzzy_grep_commands = ava::tui::filter_slash_commands("/gp", slash_commands);
  expect(fuzzy_grep_commands.size() == 1 && fuzzy_grep_commands.front().command == "/grep" && ava::tui::slash_palette_visible("/gp", slash_commands) &&
             ava::tui::slash_command_selection_text("/gp", slash_commands, 0) == "/grep ",
         "tui slash palette fuzzy-matches command names like Pi");
  expect(ava::tui::filter_slash_commands("hello", slash_commands).empty(), "tui slash palette stays hidden for normal chat input");
  expect(ava::tui::slash_palette_visible("/g", slash_commands), "tui slash palette is visible while filtering commands");
  expect(!ava::tui::slash_palette_visible("/help", slash_commands), "tui slash palette hides after an exact no-argument command");
  expect(ava::tui::slash_command_selection_text("/g", slash_commands, 1) == "/glob ",
         "tui slash selection fills argument-taking command with a trailing space");
  expect(ava::tui::slash_command_selection_text("/h", slash_commands, 0) == "/help", "tui slash selection fills no-argument command without submitting it");
  expect(ava::tui::clamp_slash_palette_selection("/g", slash_commands, 99) == 1, "tui clamps out-of-range slash palette selection to the last match");
  expect(ava::tui::previous_slash_palette_selection("/g", slash_commands, 0) == 1 && ava::tui::next_slash_palette_selection("/g", slash_commands, 1) == 0,
         "tui slash palette arrow selection wraps through filtered commands");

  std::vector<ava::tui::SlashCommandItem> const argument_slash_commands = {
      ava::tui::SlashCommandItem{
          .command = "/models",
          .description = "List models",
          .hint = "[query|provider/model]",
          .category = "Models",
          .aliases = {"/model"},
          .argument_completions = {ava::tui::SlashCommandArgumentCompletion{
                                       .value = "openai/gpt-5.5", .description = "GPT-5.5", .category = "Models", .argument_index = 0, .append_space = false},
                                   ava::tui::SlashCommandArgumentCompletion{.value = "anthropic/claude-sonnet-4-5",
                                                                            .description = "Claude Sonnet 4.5",
                                                                            .category = "Models",
                                                                            .argument_index = 0,
                                                                            .append_space = false}}},
      ava::tui::SlashCommandItem{.command = "/mcp",
                                 .description = "MCP",
                                 .hint = "<list|inspect|tools|restart> ...",
                                 .category = "Plugins",
                                 .argument_completions = {ava::tui::SlashCommandArgumentCompletion{
                                                              .value = "inspect", .description = "Inspect server", .category = "MCP", .argument_index = 0},
                                                          ava::tui::SlashCommandArgumentCompletion{.value = "fs",
                                                                                                   .description = "Filesystem server",
                                                                                                   .category = "MCP",
                                                                                                   .required_previous_args = {"inspect"},
                                                                                                   .argument_index = 1,
                                                                                                   .append_space = false}}}};
  auto const model_argument_matches = ava::tui::filter_slash_commands("/model open", argument_slash_commands);
  expect(!model_argument_matches.empty() && model_argument_matches.front().argument_completion && model_argument_matches.front().command == "openai/gpt-5.5",
         "tui slash palette ranks the strongest backend-provided argument completion after a command alias first");
  auto const fuzzy_model_argument_matches = ava::tui::filter_slash_commands("/models sonnet", argument_slash_commands);
  expect(fuzzy_model_argument_matches.size() == 1 && fuzzy_model_argument_matches.front().argument_completion &&
             fuzzy_model_argument_matches.front().command == "anthropic/claude-sonnet-4-5" &&
             ava::tui::slash_palette_visible("/models sonnet", argument_slash_commands) &&
             ava::tui::slash_command_selection_text("/models sonnet", argument_slash_commands, 0) == "/models anthropic/claude-sonnet-4-5",
         "tui slash palette fuzzy-matches non-file argument completions like Pi model search");
  auto const swapped_model_argument_matches = ava::tui::filter_slash_commands("/models 4sonnet", argument_slash_commands);
  expect(swapped_model_argument_matches.size() == 1 && swapped_model_argument_matches.front().argument_completion &&
             swapped_model_argument_matches.front().command == "anthropic/claude-sonnet-4-5",
         "tui slash palette supports Pi-style swapped numeric/name fuzzy argument queries");
  expect(ava::tui::slash_palette_visible("/models open", argument_slash_commands) &&
             ava::tui::slash_command_selection_text("/models open", argument_slash_commands, 0) == "/models openai/gpt-5.5",
         "tui slash selection inserts explicit backend-provided argument completion text");
  expect(ava::tui::slash_command_selection_text("/mcp inspect f", argument_slash_commands, 0) == "/mcp inspect fs",
         "tui argument completion preserves required previous arguments for nested command forms");
  auto const command_cursor_input = std::string("/models open");
  auto const command_cursor = std::string("/models").size();
  auto const command_cursor_matches = ava::tui::filter_slash_commands(command_cursor_input, command_cursor, argument_slash_commands);
  expect(command_cursor_matches.size() == 1 && command_cursor_matches.front().command == "/models" && !command_cursor_matches.front().argument_completion &&
             ava::tui::slash_palette_visible(command_cursor_input, command_cursor, argument_slash_commands),
         "tui slash palette re-queries command-name completions when the cursor moves before slash arguments like Pi");
  auto const command_cursor_selection = ava::tui::slash_command_selection_text("/mod open", std::string("/mod").size(), argument_slash_commands, 0);
  expect(command_cursor_selection.text == "/models open" && command_cursor_selection.cursor == std::string("/models ").size(),
         "tui cursor-scoped slash selection preserves suffix text after the command-name cursor");
  std::vector<ava::tui::SlashCommandItem> const connect_slash_commands = {
      ava::tui::SlashCommandItem{.command = "/connect", .description = "Connect a provider", .category = "General"}};
  expect(!ava::tui::slash_palette_visible("/connect", connect_slash_commands) && !ava::tui::slash_palette_visible("/connect ", connect_slash_commands) &&
             !ava::tui::slash_palette_visible("/connect openai", connect_slash_commands) &&
             ava::tui::filter_slash_commands("/connect openai ", connect_slash_commands).empty(),
         "tui slash palette lets /connect submit directly so provider and method choices stay in the centered modal");
  auto const argument_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "/model open",
                                                                                     .status = "ready",
                                                                                     .transcript = {},
                                                                                     .slash_commands = argument_slash_commands,
                                                                                     .selected_slash_command_index = 0,
                                                                                     .width = 96,
                                                                                     .height = 10});
  expect(std::ranges::any_of(argument_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("openai/gpt-5.5") != std::string::npos && visible.find("GPT-5.5") != std::string::npos &&
                                      visible.find("Models") != std::string::npos;
                             }),
         "tui slash palette renders argument completion value, category, and description");
  auto const cursor_scoped_argument_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                   .provider = "openai",
                                                                                                   .model = "gpt-5.5",
                                                                                                   .session_id = "session_test",
                                                                                                   .input = command_cursor_input,
                                                                                                   .status = "ready",
                                                                                                   .transcript = {},
                                                                                                   .slash_commands = argument_slash_commands,
                                                                                                   .selected_slash_command_index = 0,
                                                                                                   .width = 96,
                                                                                                   .height = 10,
                                                                                                   .input_cursor = command_cursor});
  expect(std::ranges::any_of(cursor_scoped_argument_palette, [](std::string const& line) { return strip_sgr(line).find("/models") != std::string::npos; }) &&
             std::ranges::none_of(cursor_scoped_argument_palette,
                                  [](std::string const& line) { return strip_sgr(line).find("openai/gpt-5.5") != std::string::npos; }),
         "tui rendered slash palette drops stale argument suggestions after cursor movement into the command name");
  std::vector<ava::tui::SlashCommandItem> const path_slash_commands = {ava::tui::SlashCommandItem{
      .command = "/read",
      .description = "Read a file",
      .hint = "<path>",
      .category = "Files",
      .argument_completions = {ava::tui::SlashCommandArgumentCompletion{
                                   .value = "src/", .description = "directory", .category = "Files", .argument_index = 0, .append_space = false},
                               ava::tui::SlashCommandArgumentCompletion{
                                   .value = "src/main.cpp", .description = "file 24 bytes", .category = "Files", .argument_index = 0, .append_space = false}}}};
  expect(ava::tui::slash_command_selection_text("/read sr", path_slash_commands, 0) == "/read src/" &&
             ava::tui::slash_palette_visible("/read src/", path_slash_commands) &&
             ava::tui::slash_command_selection_text("/read src/", path_slash_commands, 1) == "/read src/main.cpp",
         "tui slash path completion keeps directory prefixes open for nested file completions");
  expect(!ava::tui::slash_palette_visible("/read /", path_slash_commands) && ava::tui::filter_slash_commands("/read /", path_slash_commands).empty(),
         "tui slash argument palette yields when the current prefix has no backend completion match");
  auto const path_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                 .provider = "openai",
                                                                                 .model = "gpt-5.5",
                                                                                 .session_id = "session_test",
                                                                                 .input = "/read src/",
                                                                                 .status = "ready",
                                                                                 .transcript = {},
                                                                                 .slash_commands = path_slash_commands,
                                                                                 .selected_slash_command_index = 1,
                                                                                 .width = 96,
                                                                                 .height = 10});
  expect(std::ranges::any_of(path_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("src/main.cpp") != std::string::npos && visible.find("file 24 bytes") != std::string::npos &&
                                      visible.find("Files") != std::string::npos;
                             }),
         "tui slash palette renders backend-provided path completions");
  std::vector<ava::tui::FileReferenceItem> const file_references = {
      ava::tui::FileReferenceItem{.value = "src/", .description = "directory", .category = "Files", .directory = true},
      ava::tui::FileReferenceItem{.value = "src/main.cpp", .description = "file 24 bytes", .category = "Files"},
      ava::tui::FileReferenceItem{.value = "src/components/Button.tsx", .description = "file 42 bytes", .category = "Files"},
      ava::tui::FileReferenceItem{.value = "my folder/", .description = "directory", .category = "Files", .directory = true},
      ava::tui::FileReferenceItem{.value = "my folder/test file.txt", .description = "file 12 bytes", .category = "Files"}};
  auto const reference_matches = ava::tui::filter_file_references("review @sr", std::string("review @sr").size(), file_references);
  expect(reference_matches.size() >= 2 && reference_matches.front().value == "src/" &&
             std::ranges::any_of(reference_matches, [](auto const& item) { return item.value == "src/main.cpp"; }),
         "tui file reference palette filters @ prefixes against backend candidates");
  auto const fuzzy_reference_matches = ava::tui::filter_file_references("review @comp/but", std::string("review @comp/but").size(), file_references);
  expect(fuzzy_reference_matches.size() == 1 && fuzzy_reference_matches.front().value == "src/components/Button.tsx",
         "tui file reference palette fuzzy-matches slash-separated path tokens against nested candidates");
  auto const case_reference_matches = ava::tui::filter_file_references("review @MAIN", std::string("review @MAIN").size(), file_references);
  expect(case_reference_matches.size() == 1 && case_reference_matches.front().value == "src/main.cpp",
         "tui file reference palette matches @ queries case-insensitively");
  auto const spaced_reference_matches = ava::tui::filter_file_references("review @my", std::string("review @my").size(), file_references);
  expect(spaced_reference_matches.size() == 2 && spaced_reference_matches.front().value == "my folder/",
         "tui file reference palette includes paths with spaces and ranks matching directories first");
  auto const equals_reference_matches = ava::tui::filter_file_references("include=@sr", std::string("include=@sr").size(), file_references);
  expect(equals_reference_matches.size() >= 2 && equals_reference_matches.front().value == "src/",
         "tui file reference palette treats equals as a token delimiter like Pi");
  auto const single_quote_reference_matches = ava::tui::filter_file_references("include='@sr", std::string("include='@sr").size(), file_references);
  expect(single_quote_reference_matches.size() >= 2 && single_quote_reference_matches.front().value == "src/",
         "tui file reference palette treats single quotes as token delimiters like Pi");
  auto const parenthesized_reference_matches = ava::tui::filter_file_references("compare (@sr", std::string("compare (@sr").size(), file_references);
  expect(parenthesized_reference_matches.size() >= 2 && parenthesized_reference_matches.front().value == "src/",
         "tui file reference palette opens after parenthesized prose boundaries");
  auto const bracketed_reference_matches = ava::tui::filter_file_references("compare [@sr", std::string("compare [@sr").size(), file_references);
  expect(bracketed_reference_matches.size() >= 2 && bracketed_reference_matches.front().value == "src/",
         "tui file reference palette opens after bracketed prose boundaries");
  auto const reference_selection = ava::tui::file_reference_selection_text("review @main please", std::string("review @main").size(), file_references, 0);
  expect(reference_selection.text == "review @src/main.cpp please" && reference_selection.cursor == std::string("review @src/main.cpp").size(),
         "tui file reference selection replaces only the active @ token and leaves surrounding draft text intact");
  auto const spaced_directory_selection = ava::tui::file_reference_selection_text("review @my", std::string("review @my").size(), file_references, 0);
  expect(spaced_directory_selection.text == "review @\"my folder/\"" && spaced_directory_selection.cursor == std::string("review @\"my folder/").size(),
         "tui file reference selection quotes directories with spaces and leaves the cursor inside the quote");
  auto const spaced_file_selection = ava::tui::file_reference_selection_text("review @my", std::string("review @my").size(), file_references, 1);
  expect(spaced_file_selection.text == "review @\"my folder/test file.txt\" " && spaced_file_selection.cursor == spaced_file_selection.text.size(),
         "tui file reference selection quotes files with spaces and appends a safe separator");
  auto const quoted_file_selection =
      ava::tui::file_reference_selection_text("review @\"my folder/te\"", std::string("review @\"my folder/te").size(), file_references, 1);
  expect(quoted_file_selection.text == "review @\"my folder/test file.txt\" " && quoted_file_selection.cursor == quoted_file_selection.text.size(),
         "tui file reference selection completes inside quoted @ paths without duplicating the closing quote");
  auto const equals_reference_selection = ava::tui::file_reference_selection_text("include=@main", std::string("include=@main").size(), file_references, 0);
  expect(equals_reference_selection.text == "include=@src/main.cpp " && equals_reference_selection.cursor == equals_reference_selection.text.size(),
         "tui file reference selection replaces only the @ token after an equals delimiter");
  auto const single_quote_reference_selection =
      ava::tui::file_reference_selection_text("include='@main", std::string("include='@main").size(), file_references, 0);
  expect(single_quote_reference_selection.text == "include='@src/main.cpp " &&
             single_quote_reference_selection.cursor == single_quote_reference_selection.text.size(),
         "tui file reference selection replaces only the @ token after a single-quote delimiter");
  auto const parenthesized_reference_selection =
      ava::tui::file_reference_selection_text("compare (@main)", std::string("compare (@main").size(), file_references, 0);
  expect(parenthesized_reference_selection.text == "compare (@src/main.cpp)" &&
             parenthesized_reference_selection.cursor == std::string("compare (@src/main.cpp").size(),
         "tui file reference selection preserves closing punctuation without inserting an extra space");
  auto const reference_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                      .provider = "openai",
                                                                                      .model = "gpt-5.5",
                                                                                      .session_id = "session_test",
                                                                                      .input = "review @sr",
                                                                                      .status = "ready",
                                                                                      .transcript = {},
                                                                                      .file_references = file_references,
                                                                                      .selected_slash_command_index = 1,
                                                                                      .width = 96,
                                                                                      .height = 10,
                                                                                      .input_cursor = std::string("review @sr").size()});
  auto const reference_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                             .provider = "openai",
                                                             .model = "gpt-5.5",
                                                             .session_id = "session_test",
                                                             .input = "review @sr",
                                                             .status = "ready",
                                                             .transcript = {},
                                                             .file_references = file_references,
                                                             .selected_slash_command_index = 1,
                                                             .width = 96,
                                                             .height = 10,
                                                             .input_cursor = std::string("review @sr").size()};
  auto const reference_hit_row =
      static_cast<std::size_t>(
          std::ranges::find_if(reference_palette, [](std::string const& line) { return strip_sgr(line).find("@src/main.cpp") != std::string::npos; }) -
          reference_palette.begin()) +
      1;
  auto const expected_reference_index = static_cast<std::size_t>(
      std::ranges::find_if(reference_matches, [](auto const& item) { return item.value == "src/main.cpp"; }) - reference_matches.begin());
  auto const clicked_reference = ava::tui::file_reference_palette_selection_for_screen_row(reference_snapshot, reference_hit_row);
  expect(std::ranges::any_of(reference_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("@src/main.cpp") != std::string::npos && visible.find("file 24 bytes") != std::string::npos &&
                                      visible.find("Files") != std::string::npos;
                             }) &&
             clicked_reference && *clicked_reference == expected_reference_index &&
             !ava::tui::file_reference_palette_selection_for_screen_row(reference_snapshot, 1),
         "tui file reference palette renders @ candidates and hit-tests visible rows to candidate indices");
  auto const spaced_reference_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                             .provider = "openai",
                                                                                             .model = "gpt-5.5",
                                                                                             .session_id = "session_test",
                                                                                             .input = "review @my",
                                                                                             .status = "ready",
                                                                                             .transcript = {},
                                                                                             .file_references = file_references,
                                                                                             .selected_slash_command_index = 0,
                                                                                             .width = 96,
                                                                                             .height = 10,
                                                                                             .input_cursor = std::string("review @my").size()});
  expect(std::ranges::any_of(spaced_reference_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("@\"my folder/\"") != std::string::npos && visible.find("directory") != std::string::npos;
                             }),
         "tui file reference palette renders quoted @ candidates for paths with spaces");
  auto const normal_path_matches = ava::tui::filter_path_completions("inspect src/", std::string("inspect src/").size(), file_references);
  expect(normal_path_matches.size() >= 2 && normal_path_matches.front().value == "src/" &&
             std::ranges::any_of(normal_path_matches, [](auto const& item) { return item.value == "src/main.cpp"; }),
         "tui normal path completion filters path-like prompt tokens against backend candidates");
  auto const equals_path_matches = ava::tui::filter_path_completions("inspect file=src/", std::string("inspect file=src/").size(), file_references);
  expect(equals_path_matches.size() >= 2 && equals_path_matches.front().value == "src/", "tui normal path completion treats equals as a path token delimiter");
  auto const single_quote_path_matches = ava::tui::filter_path_completions("inspect path='src/", std::string("inspect path='src/").size(), file_references);
  expect(single_quote_path_matches.size() >= 2 && single_quote_path_matches.front().value == "src/",
         "tui normal path completion treats single quotes as path token delimiters like Pi");
  auto const whitespace_path_matches = ava::tui::filter_path_completions("inspect ", std::string("inspect ").size(), file_references);
  expect(whitespace_path_matches.empty(), "tui normal path completion stays closed at a new token after whitespace");
  expect(!ava::tui::path_completion_palette_visible("inspect ", std::string("inspect ").size(), file_references),
         "tui normal path completion palette stays hidden at a new token after whitespace");
  auto const forced_whitespace_path_matches = ava::tui::filter_path_completions("inspect ", std::string("inspect ").size(), file_references, true);
  expect(forced_whitespace_path_matches.size() == file_references.size() && forced_whitespace_path_matches.front().directory,
         "tui forced path completion can open workspace suggestions at a new token after whitespace");
  expect(ava::tui::filter_path_completions("inspect file=", std::string("inspect file=").size(), file_references).empty(),
         "tui normal path completion does not open an empty token after equals without explicit Tab");
  expect(ava::tui::filter_path_completions("inspect main", std::string("inspect main").size(), file_references).empty(),
         "tui normal path completion does not open for ordinary prose tokens");
  auto const forced_path_matches = ava::tui::filter_path_completions("inspect main", std::string("inspect main").size(), file_references, true);
  expect(forced_path_matches.size() == 1 && forced_path_matches.front().value == "src/main.cpp", "tui forced path completion can match a bare prompt token");
  auto const forced_equals_path_matches =
      ava::tui::filter_path_completions("inspect file=main", std::string("inspect file=main").size(), file_references, true);
  expect(forced_equals_path_matches.size() == 1 && forced_equals_path_matches.front().value == "src/main.cpp",
         "tui forced path completion matches the token after an equals delimiter");
  auto const forced_empty_matches = ava::tui::filter_path_completions("", 0, file_references, true);
  expect(forced_empty_matches.size() == file_references.size() && forced_empty_matches.front().directory,
         "tui forced path completion can open workspace suggestions from an empty draft");
  expect(ava::tui::filter_path_completions("/read", std::string("/read").size(), file_references, true).empty(),
         "tui forced path completion leaves top-level slash command names to the slash palette");
  auto const forced_slash_argument_matches = ava::tui::filter_path_completions("/read main", std::string("/read main").size(), file_references, true);
  expect(forced_slash_argument_matches.size() == 1 && forced_slash_argument_matches.front().value == "src/main.cpp",
         "tui forced path completion can match the current slash-command argument token like Pi");
  expect(ava::tui::filter_path_completions("inspect @src/", std::string("inspect @src/").size(), file_references).empty(),
         "tui normal path completion does not steal @ file reference tokens");
  auto const dot_slash_path_selection = ava::tui::path_completion_selection_text("inspect ./sr", std::string("inspect ./sr").size(), file_references, 0);
  expect(dot_slash_path_selection.text == "inspect ./src/" && dot_slash_path_selection.cursor == std::string("inspect ./src/").size(),
         "tui normal path completion preserves a typed dot-slash prefix");
  auto const quoted_path_selection =
      ava::tui::path_completion_selection_text("inspect \"my folder/te\"", std::string("inspect \"my folder/te").size(), file_references, 0);
  expect(quoted_path_selection.text == "inspect \"my folder/test file.txt\"" && quoted_path_selection.cursor == quoted_path_selection.text.size(),
         "tui normal path completion continues inside quoted paths without duplicating the closing quote");
  auto const equals_path_selection =
      ava::tui::path_completion_selection_text("inspect file=src/ma", std::string("inspect file=src/ma").size(), file_references, 0);
  expect(equals_path_selection.text == "inspect file=src/main.cpp" && equals_path_selection.cursor == equals_path_selection.text.size(),
         "tui normal path completion preserves text before an equals delimiter");
  auto const single_quote_path_selection =
      ava::tui::path_completion_selection_text("inspect path='src/ma", std::string("inspect path='src/ma").size(), file_references, 0);
  expect(single_quote_path_selection.text == "inspect path='src/main.cpp" && single_quote_path_selection.cursor == single_quote_path_selection.text.size(),
         "tui normal path completion preserves text before a single-quote delimiter");
  auto const equals_quoted_path_selection =
      ava::tui::path_completion_selection_text("inspect path=\"my folder/te\"", std::string("inspect path=\"my folder/te").size(), file_references, 0);
  expect(equals_quoted_path_selection.text == "inspect path=\"my folder/test file.txt\"" &&
             equals_quoted_path_selection.cursor == equals_quoted_path_selection.text.size(),
         "tui normal path completion continues inside quoted paths after equals delimiters");
  auto const whitespace_path_selection = ava::tui::path_completion_selection_text("inspect ", std::string("inspect ").size(), file_references, 1);
  expect(whitespace_path_selection.text == "inspect " && whitespace_path_selection.cursor == std::string("inspect ").size(),
         "tui normal path completion selection leaves text and cursor unchanged at an empty token after whitespace");
  auto const forced_bare_path_selection =
      ava::tui::path_completion_selection_text("inspect main", std::string("inspect main").size(), file_references, 0, true);
  expect(forced_bare_path_selection.text == "inspect src/main.cpp" && forced_bare_path_selection.cursor == forced_bare_path_selection.text.size(),
         "tui forced path completion replaces a bare token with the selected relative path");
  auto const forced_slash_argument_selection =
      ava::tui::path_completion_selection_text("/read main", std::string("/read main").size(), file_references, 0, true);
  expect(forced_slash_argument_selection.text == "/read src/main.cpp" && forced_slash_argument_selection.cursor == forced_slash_argument_selection.text.size(),
         "tui forced path completion replaces only the active slash-command argument token");
  auto const normal_path_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                        .provider = "openai",
                                                                                        .model = "gpt-5.5",
                                                                                        .session_id = "session_test",
                                                                                        .input = "inspect src/",
                                                                                        .status = "ready",
                                                                                        .transcript = {},
                                                                                        .file_references = file_references,
                                                                                        .selected_slash_command_index = 1,
                                                                                        .width = 96,
                                                                                        .height = 10,
                                                                                        .input_cursor = std::string("inspect src/").size()});
  auto const normal_path_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                               .provider = "openai",
                                                               .model = "gpt-5.5",
                                                               .session_id = "session_test",
                                                               .input = "inspect src/",
                                                               .status = "ready",
                                                               .transcript = {},
                                                               .file_references = file_references,
                                                               .selected_slash_command_index = 1,
                                                               .width = 96,
                                                               .height = 10,
                                                               .input_cursor = std::string("inspect src/").size()};
  auto const path_hit_row = static_cast<std::size_t>(std::ranges::find_if(normal_path_palette,
                                                                          [](std::string const& line) {
                                                                            auto const visible = strip_sgr(line);
                                                                            return visible.find("src/main.cpp") != std::string::npos &&
                                                                                   visible.find("file 24 bytes") != std::string::npos;
                                                                          }) -
                                                     normal_path_palette.begin()) +
                            1;
  auto const expected_path_index = static_cast<std::size_t>(
      std::ranges::find_if(normal_path_matches, [](auto const& item) { return item.value == "src/main.cpp"; }) - normal_path_matches.begin());
  auto const clicked_path = ava::tui::path_completion_palette_selection_for_screen_row(normal_path_snapshot, path_hit_row);
  expect(std::ranges::any_of(normal_path_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("src/main.cpp") != std::string::npos && visible.find("file 24 bytes") != std::string::npos &&
                                      visible.find("Files") != std::string::npos;
                             }) &&
             clicked_path && *clicked_path == expected_path_index && !ava::tui::path_completion_palette_selection_for_screen_row(normal_path_snapshot, 1),
         "tui normal path completion palette renders backend-provided path candidates and hit-tests visible rows");
  auto const forced_path_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                        .provider = "openai",
                                                                                        .model = "gpt-5.5",
                                                                                        .session_id = "session_test",
                                                                                        .input = "inspect src",
                                                                                        .status = "ready",
                                                                                        .transcript = {},
                                                                                        .file_references = file_references,
                                                                                        .selected_slash_command_index = 0,
                                                                                        .path_completion_force_active = true,
                                                                                        .width = 96,
                                                                                        .height = 10,
                                                                                        .input_cursor = std::string("inspect src").size()});
  expect(std::ranges::any_of(forced_path_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("src/") != std::string::npos && visible.find("directory") != std::string::npos;
                             }),
         "tui forced path completion palette renders for bare prompt tokens after Tab");

  auto const key_bindings = ava::tui::parse_key_bindings_json(
      "{\"submit\":\"Ctrl+T, Enter\",\"new_line\":\"Shift+Enter, Ctrl+Enter\",\"message_follow_up\":\"Alt+Enter\","
      "\"delete_to_line_start\":\"Ctrl+U\","
      "\"autocomplete_accept\":\"Tab\",\"variant_cycle\":\"Ctrl+D, Shift+Tab\",\"undo\":\"Ctrl+-\","
      "\"suspend\":\"Ctrl+Z\",\"clipboard_paste_image\":\"Ctrl+V\",\"redo\":\"Ctrl+R\",\"delete_forward\":\"Delete\","
      "\"cursor_word_left\":\"Ctrl+Left, Alt+Left, Alt+B\",\"cursor_word_right\":\"Ctrl+Right, Alt+Right, Alt+F\","
      "\"jump_forward\":\"Ctrl+]\",\"jump_backward\":\"Ctrl+Alt+]\","
      "\"delete_word_backward\":\"Ctrl+W, Alt+Backspace\",\"delete_word_forward\":\"Alt+D, Alt+Delete\","
      "\"details_toggle\":\"Ctrl+O\",\"model_select\":\"Ctrl+L\",\"model_cycle_forward\":\"Ctrl+P\","
      "\"model_cycle_backward\":\"Shift+Ctrl+P\",\"app.models.save\":\"Ctrl+S\","
      "\"app.models.enableAll\":\"Ctrl+A\",\"app.models.clearAll\":\"Ctrl+X\","
      "\"app.models.toggleProvider\":\"Ctrl+P\",\"app.models.reorderUp\":\"Alt+Up\","
      "\"app.models.reorderDown\":\"Alt+Down\",\"message_dequeue\":\"Alt+Up\",\"yank_pop\":\"Alt+Y\"}");
  auto const parsed_ctrl_s = ava::tui::parse_key_name("Ctrl+S");
  auto const parsed_ctrl_l = ava::tui::parse_key_name("Ctrl+L");
  auto const parsed_ctrl_shift_p = ava::tui::parse_key_name("Shift+Ctrl+P");
  auto const parsed_ctrl_g = ava::tui::parse_key_name("Ctrl+G");
  auto const parsed_alt_up = ava::tui::parse_key_name("Alt+Up");
  auto const parsed_alt_down = ava::tui::parse_key_name("Alt+Down");
  auto const parsed_ctrl_x = ava::tui::parse_key_name("Ctrl+X");
  auto const parsed_ctrl_enter = ava::tui::parse_key_name("Ctrl+Enter");
  auto const parsed_alt_enter = ava::tui::parse_key_name("Alt+Enter");
  auto const parsed_space = ava::tui::parse_key_name("Space");
  auto const parsed_ctrl_space = ava::tui::parse_key_name("Ctrl+Space");
  auto const parsed_ctrl_1 = ava::tui::parse_key_name("Ctrl+1");
  auto const parsed_ctrl_9 = ava::tui::parse_key_name("Ctrl+9");
  auto const parsed_shift_tab = ava::tui::parse_key_name("Shift+Tab");
  auto const parsed_shift_l = ava::tui::parse_key_name("Shift+L");
  auto const parsed_shift_t = ava::tui::parse_key_name("Shift+T");
  auto const parsed_ctrl_minus = ava::tui::parse_key_name("Ctrl+-");
  auto const parsed_ctrl_slash = ava::tui::parse_key_name("Ctrl+/");
  auto const parsed_ctrl_o = ava::tui::parse_key_name("Ctrl+O");
  auto const parsed_ctrl_v = ava::tui::parse_key_name("Ctrl+V");
  auto const parsed_ctrl_right_bracket = ava::tui::parse_key_name("Ctrl+]");
  auto const parsed_ctrl_alt_right_bracket = ava::tui::parse_key_name("Ctrl+Alt+]");
  auto const parsed_shift_up = ava::tui::parse_key_name("Shift+Up");
  auto const parsed_shift_down = ava::tui::parse_key_name("Shift+Down");
  auto const parsed_shift_left = ava::tui::parse_key_name("Shift+Left");
  auto const parsed_shift_right = ava::tui::parse_key_name("Shift+Right");
  auto const parsed_shift_ctrl_left = ava::tui::parse_key_name("Shift+Ctrl+Left");
  auto const parsed_ctrl_shift_right = ava::tui::parse_key_name("Ctrl+Shift+Right");
  auto const parsed_shift_alt_left = ava::tui::parse_key_name("Shift+Alt+Left");
  auto const parsed_alt_shift_right = ava::tui::parse_key_name("Alt+Shift+Right");
  auto const parsed_alt_left = ava::tui::parse_key_name("Alt+Left");
  auto const parsed_alt_right = ava::tui::parse_key_name("Alt+Right");
  auto const parsed_shift_backspace = ava::tui::parse_key_name("Shift+Backspace");
  auto const parsed_ctrl_backspace = ava::tui::parse_key_name("Ctrl+Backspace");
  auto const parsed_shift_delete = ava::tui::parse_key_name("Shift+Delete");
  auto const parsed_insert = ava::tui::parse_key_name("Insert");
  auto const parsed_clear = ava::tui::parse_key_name("Clear");
  auto const parsed_alt_backspace = ava::tui::parse_key_name("Alt+Backspace");
  auto const parsed_alt_d = ava::tui::parse_key_name("Alt+D");
  auto const parsed_alt_delete = ava::tui::parse_key_name("Alt+Delete");
  auto const parsed_alt_h = ava::tui::parse_key_name("Alt+H");
  auto const parsed_alt_j = ava::tui::parse_key_name("Alt+J");
  auto const parsed_alt_k = ava::tui::parse_key_name("Alt+K");
  auto const parsed_alt_l = ava::tui::parse_key_name("Alt+L");
  auto const parsed_alt_w = ava::tui::parse_key_name("Alt+W");
  auto const parsed_home = ava::tui::parse_key_name("Home");
  auto const parsed_end = ava::tui::parse_key_name("End");
  auto const parsed_ctrl_home = ava::tui::parse_key_name("Ctrl+Home");
  auto const parsed_ctrl_end = ava::tui::parse_key_name("Ctrl+End");
  auto const parsed_shift_home = ava::tui::parse_key_name("Shift+Home");
  auto const parsed_shift_end = ava::tui::parse_key_name("Shift+End");
  auto const parsed_shift_ctrl_home = ava::tui::parse_key_name("Shift+Ctrl+Home");
  auto const parsed_ctrl_shift_end = ava::tui::parse_key_name("Ctrl+Shift+End");
  auto const parsed_f2 = ava::tui::parse_key_name("F2");
  auto const parsed_f12 = ava::tui::parse_key_name("F12");
  expect(
      key_bindings && ava::tui::action_for_key(*key_bindings, ava::tui::Key::CtrlT) == ava::tui::TuiAction::Submit &&
          ava::tui::action_for_key(*key_bindings, ava::tui::Key::CtrlD) == ava::tui::TuiAction::VariantCycle &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::VariantCycle, ava::tui::Key::ShiftTab) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::CursorWordLeft, ava::tui::Key::CtrlArrowLeft) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::CursorWordLeft, ava::tui::Key::AltArrowLeft) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::CursorWordLeft, ava::tui::Key::AltB) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::CursorWordRight, ava::tui::Key::CtrlArrowRight) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::CursorWordRight, ava::tui::Key::AltArrowRight) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::CursorWordRight, ava::tui::Key::AltF) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::JumpForward, ava::tui::Key::CtrlRightBracket) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::JumpBackward, ava::tui::Key::CtrlAltRightBracket) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DeleteForward, ava::tui::Key::Delete) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DeleteWordBackward, ava::tui::Key::AltBackspace) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DeleteWordForward, ava::tui::Key::AltD) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DeleteWordForward, ava::tui::Key::AltDelete) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::NewLine, ava::tui::Key::CtrlEnter) &&
          !ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::NewLine, ava::tui::Key::AltEnter) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::MessageFollowUp, ava::tui::Key::AltEnter) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::Undo, ava::tui::Key::CtrlMinus) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::Suspend, ava::tui::Key::CtrlZ) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ClipboardPasteImage, ava::tui::Key::CtrlV) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::Redo, ava::tui::Key::CtrlR) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlO) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelSelect, ava::tui::Key::CtrlL) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelCycleForward, ava::tui::Key::CtrlP) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelCycleBackward, ava::tui::Key::CtrlShiftP) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelsSave, ava::tui::Key::CtrlS) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelsEnableAll, ava::tui::Key::CtrlA) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelsClearAll, ava::tui::Key::CtrlX) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelsToggleProvider, ava::tui::Key::CtrlP) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelsReorderUp, ava::tui::Key::AltArrowUp) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::ModelsReorderDown, ava::tui::Key::AltArrowDown) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::MessageDequeue, ava::tui::Key::AltArrowUp) && parsed_ctrl_s &&
          ava::tui::key_display(*parsed_ctrl_s) == "Ctrl+S" && parsed_ctrl_l && ava::tui::key_display(*parsed_ctrl_l) == "Ctrl+L" && parsed_ctrl_shift_p &&
          ava::tui::key_display(*parsed_ctrl_shift_p) == "Shift+Ctrl+P" && parsed_ctrl_g && ava::tui::key_display(*parsed_ctrl_g) == "Ctrl+G" &&
          parsed_alt_up && ava::tui::key_display(*parsed_alt_up) == "Alt+Up" && parsed_alt_down && ava::tui::key_display(*parsed_alt_down) == "Alt+Down" &&
          parsed_ctrl_x && ava::tui::key_display(*parsed_ctrl_x) == "Ctrl+X" && parsed_ctrl_enter &&
          ava::tui::key_display(*parsed_ctrl_enter) == "Ctrl+Enter" && parsed_alt_enter && ava::tui::key_display(*parsed_alt_enter) == "Alt+Enter" &&
          parsed_space && ava::tui::key_display(*parsed_space) == "Space" && parsed_ctrl_space && ava::tui::key_display(*parsed_ctrl_space) == "Ctrl+Space" &&
          parsed_ctrl_1 && ava::tui::key_display(*parsed_ctrl_1) == "Ctrl+1" && parsed_ctrl_9 && ava::tui::key_display(*parsed_ctrl_9) == "Ctrl+9" &&
          parsed_shift_tab && ava::tui::key_display(*parsed_shift_tab) == "Shift+Tab" && parsed_shift_l &&
          ava::tui::key_display(*parsed_shift_l) == "Shift+L" && parsed_shift_t && ava::tui::key_display(*parsed_shift_t) == "Shift+T" && parsed_ctrl_minus &&
          ava::tui::key_display(*parsed_ctrl_minus) == "Ctrl+-" && parsed_ctrl_slash && ava::tui::key_display(*parsed_ctrl_slash) == "Ctrl+/" &&
          parsed_ctrl_o && ava::tui::key_display(*parsed_ctrl_o) == "Ctrl+O" && parsed_ctrl_v && ava::tui::key_display(*parsed_ctrl_v) == "Ctrl+V" &&
          parsed_ctrl_right_bracket && ava::tui::key_display(*parsed_ctrl_right_bracket) == "Ctrl+]" && parsed_ctrl_alt_right_bracket &&
          ava::tui::key_display(*parsed_ctrl_alt_right_bracket) == "Ctrl+Alt+]" && parsed_shift_up && ava::tui::key_display(*parsed_shift_up) == "Shift+Up" &&
          parsed_shift_down && ava::tui::key_display(*parsed_shift_down) == "Shift+Down" && parsed_shift_left &&
          ava::tui::key_display(*parsed_shift_left) == "Shift+Left" && parsed_shift_right && ava::tui::key_display(*parsed_shift_right) == "Shift+Right" &&
          parsed_shift_ctrl_left && ava::tui::key_display(*parsed_shift_ctrl_left) == "Shift+Ctrl+Left" && parsed_ctrl_shift_right &&
          ava::tui::key_display(*parsed_ctrl_shift_right) == "Shift+Ctrl+Right" && parsed_shift_alt_left &&
          ava::tui::key_display(*parsed_shift_alt_left) == "Shift+Alt+Left" && parsed_alt_shift_right &&
          ava::tui::key_display(*parsed_alt_shift_right) == "Shift+Alt+Right" && parsed_alt_left && ava::tui::key_display(*parsed_alt_left) == "Alt+Left" &&
          parsed_alt_right && ava::tui::key_display(*parsed_alt_right) == "Alt+Right" && parsed_shift_backspace &&
          ava::tui::key_display(*parsed_shift_backspace) == "Shift+Backspace" && parsed_ctrl_backspace &&
          ava::tui::key_display(*parsed_ctrl_backspace) == "Ctrl+Backspace" && parsed_shift_delete &&
          ava::tui::key_display(*parsed_shift_delete) == "Shift+Delete" && parsed_insert && ava::tui::key_display(*parsed_insert) == "Insert" && parsed_clear &&
          ava::tui::key_display(*parsed_clear) == "Clear" && parsed_alt_backspace && ava::tui::key_display(*parsed_alt_backspace) == "Alt+Backspace" &&
          parsed_alt_d && ava::tui::key_display(*parsed_alt_d) == "Alt+D" && parsed_alt_delete && ava::tui::key_display(*parsed_alt_delete) == "Alt+Delete" &&
          parsed_alt_h && ava::tui::key_display(*parsed_alt_h) == "Alt+H" && parsed_alt_j && ava::tui::key_display(*parsed_alt_j) == "Alt+J" && parsed_alt_k &&
          ava::tui::key_display(*parsed_alt_k) == "Alt+K" && parsed_alt_l && ava::tui::key_display(*parsed_alt_l) == "Alt+L" && parsed_alt_w &&
          ava::tui::key_display(*parsed_alt_w) == "Alt+W" && parsed_home && ava::tui::key_display(*parsed_home) == "Home" && parsed_end &&
          ava::tui::key_display(*parsed_end) == "End" && parsed_ctrl_home && ava::tui::key_display(*parsed_ctrl_home) == "Ctrl+Home" && parsed_ctrl_end &&
          ava::tui::key_display(*parsed_ctrl_end) == "Ctrl+End" && parsed_shift_home && ava::tui::key_display(*parsed_shift_home) == "Shift+Home" &&
          parsed_shift_end && ava::tui::key_display(*parsed_shift_end) == "Shift+End" && parsed_shift_ctrl_home &&
          ava::tui::key_display(*parsed_shift_ctrl_home) == "Shift+Ctrl+Home" && parsed_ctrl_shift_end &&
          ava::tui::key_display(*parsed_ctrl_shift_end) == "Shift+Ctrl+End" && parsed_f2 && ava::tui::key_display(*parsed_f2) == "F2" && parsed_f12 &&
          ava::tui::key_display(*parsed_f12) == "F12" && ava::tui::parse_key_name("Ctrl+H") == ava::tui::Key::CtrlH &&
          ava::tui::key_display(ava::tui::Key::CtrlH) == "Ctrl+H" &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::YankPop, ava::tui::Key::AltY) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::DeleteToLineStart, ava::tui::Key::CtrlU) &&
          ava::tui::key_matches_action(*key_bindings, ava::tui::TuiAction::AutocompleteAccept, ava::tui::Key::Tab) &&
          ava::tui::keys_display(*key_bindings, ava::tui::TuiAction::CursorWordRight).find("Ctrl+Right") != std::string::npos &&
          ava::tui::keys_display(*key_bindings, ava::tui::TuiAction::Submit).find("Ctrl+T") != std::string::npos,
      "tui keybind parser maps configured keys to semantic actions and display text");
  auto const function_key_bindings = ava::tui::parse_key_bindings_json("{\"tui.editor.cursorLineEnd\":\"F2\"}");
  expect(function_key_bindings && ava::tui::key_matches_action(*function_key_bindings, ava::tui::TuiAction::CursorLineEnd, ava::tui::Key::F2),
         "tui keybind parser accepts Pi-style function keys for configurable actions");
  auto const special_key_bindings = ava::tui::parse_key_bindings_json("{\"tui.editor.cursorLineStart\":\"Insert\",\"tui.editor.cursorLineEnd\":\"Clear\"}");
  expect(special_key_bindings && ava::tui::key_matches_action(*special_key_bindings, ava::tui::TuiAction::CursorLineStart, ava::tui::Key::Insert) &&
             ava::tui::key_matches_action(*special_key_bindings, ava::tui::TuiAction::CursorLineEnd, ava::tui::Key::Clear),
         "tui keybind parser accepts Pi-style Insert and Clear special keys");
  auto const ctrl_digit_bindings = ava::tui::parse_key_bindings_json("{\"tui.editor.cursorLineEnd\":\"Ctrl+1\"}");
  expect(ctrl_digit_bindings && ava::tui::key_matches_action(*ctrl_digit_bindings, ava::tui::TuiAction::CursorLineEnd, ava::tui::Key::Ctrl1),
         "tui keybind parser accepts Pi-style Ctrl+digit special keys");
  auto const default_bindings = ava::tui::default_key_bindings();
  expect(!ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::HistoryPrev, ava::tui::Key::ArrowUp) &&
             !ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::HistoryNext, ava::tui::Key::ArrowDown) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::NewLine, ava::tui::Key::ShiftEnter) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::NewLine, ava::tui::Key::CtrlEnter) &&
             !ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::NewLine, ava::tui::Key::AltEnter) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::MessageFollowUp, ava::tui::Key::AltEnter) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::PalettePrev, ava::tui::Key::ArrowUp) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SelectPrev, ava::tui::Key::ArrowUp) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SelectNext, ava::tui::Key::ArrowDown) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SelectPageUp, ava::tui::Key::PageUp) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SelectPageDown, ava::tui::Key::PageDown) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SelectConfirm, ava::tui::Key::Enter) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SelectCancel, ava::tui::Key::Escape) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SelectCancel, ava::tui::Key::CtrlC) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SessionTogglePath, ava::tui::Key::CtrlP) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SessionToggleSort, ava::tui::Key::CtrlS) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SessionToggleSort, ava::tui::Key::CtrlT) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SessionToggleNamedFilter, ava::tui::Key::CtrlN) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SessionRename, ava::tui::Key::CtrlR) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SessionArchive, ava::tui::Key::CtrlD) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::SessionArchiveNoninvasive, ava::tui::Key::CtrlBackspace) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::TreeFoldOrUp, ava::tui::Key::CtrlArrowLeft) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::TreeFoldOrUp, ava::tui::Key::AltArrowLeft) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::TreeUnfoldOrDown, ava::tui::Key::CtrlArrowRight) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::TreeUnfoldOrDown, ava::tui::Key::AltArrowRight) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::TreeEditLabel, ava::tui::Key::ShiftL) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::TreeToggleLabelTimestamp, ava::tui::Key::ShiftT) &&
             !ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorUp, ava::tui::Key::ArrowUp) &&
             !ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorDown, ava::tui::Key::ArrowDown) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorWordLeft, ava::tui::Key::CtrlArrowLeft) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorWordLeft, ava::tui::Key::AltArrowLeft) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorWordLeft, ava::tui::Key::AltB) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorWordRight, ava::tui::Key::CtrlArrowRight) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorWordRight, ava::tui::Key::AltArrowRight) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorWordRight, ava::tui::Key::AltF) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::JumpForward, ava::tui::Key::CtrlRightBracket) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::JumpBackward, ava::tui::Key::CtrlAltRightBracket) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorLineStart, ava::tui::Key::Home) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CursorLineEnd, ava::tui::Key::End) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteForward, ava::tui::Key::Delete) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteForward, ava::tui::Key::ShiftDelete) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteForward, ava::tui::Key::CtrlD) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteBackward, ava::tui::Key::ShiftBackspace) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteBackward, ava::tui::Key::CtrlH) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Exit, ava::tui::Key::CtrlD) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteWordBackward, ava::tui::Key::AltBackspace) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteWordForward, ava::tui::Key::AltD) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DeleteWordForward, ava::tui::Key::AltDelete) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModeToggle, ava::tui::Key::Tab) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::AutocompleteAccept, ava::tui::Key::Tab) &&
             !ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Undo, ava::tui::Key::CtrlZ) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Undo, ava::tui::Key::CtrlMinus) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Suspend, ava::tui::Key::CtrlZ) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ClipboardPasteImage, ava::tui::Key::CtrlV) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Redo, ava::tui::Key::CtrlR) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Yank, ava::tui::Key::CtrlY) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::YankPop, ava::tui::Key::AltY) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlO) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::CopySelection, ava::tui::Key::CtrlC) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ClearInput, ava::tui::Key::CtrlC) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ExternalEditor, ava::tui::Key::CtrlG) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::VariantCycle, ava::tui::Key::ShiftTab) &&
             !ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::VariantCycle, ava::tui::Key::CtrlT) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ThinkingToggle, ava::tui::Key::CtrlT) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelSelect, ava::tui::Key::CtrlL) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelCycleForward, ava::tui::Key::CtrlP) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelCycleBackward, ava::tui::Key::CtrlShiftP) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelsSave, ava::tui::Key::CtrlS) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelsEnableAll, ava::tui::Key::CtrlA) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelsClearAll, ava::tui::Key::CtrlX) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelsToggleProvider, ava::tui::Key::CtrlP) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelsReorderUp, ava::tui::Key::AltArrowUp) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::ModelsReorderDown, ava::tui::Key::AltArrowDown) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::MessageDequeue, ava::tui::Key::AltArrowUp) &&
             ava::tui::key_matches_action(default_bindings, ava::tui::TuiAction::Interrupt, ava::tui::Key::CtrlC),
         "tui default keybinds preserve context-specific semantic actions for shared keys");
  auto const navigation_key_bindings =
      ava::tui::parse_key_bindings_json("{\"message_prev\":\"PageUp\",\"message_next\":\"PageDown\",\"jump_to_bottom\":\"Ctrl+T\"}");
  expect(
      navigation_key_bindings && ava::tui::key_matches_action(*navigation_key_bindings, ava::tui::TuiAction::MessagePrev, ava::tui::Key::PageUp) &&
          ava::tui::key_matches_action(*navigation_key_bindings, ava::tui::TuiAction::MessageNext, ava::tui::Key::PageDown) &&
          ava::tui::key_matches_action(*navigation_key_bindings, ava::tui::TuiAction::JumpToBottom, ava::tui::Key::CtrlT) &&
          ava::tui::action_name(ava::tui::TuiAction::CursorUp) == "cursor_up" && ava::tui::action_name(ava::tui::TuiAction::CursorDown) == "cursor_down" &&
          ava::tui::action_name(ava::tui::TuiAction::JumpForward) == "jump_forward" &&
          ava::tui::action_name(ava::tui::TuiAction::JumpBackward) == "jump_backward" &&
          ava::tui::action_name(ava::tui::TuiAction::DeleteWordForward) == "delete_word_forward" &&
          ava::tui::action_name(ava::tui::TuiAction::SelectPrev) == "select_prev" && ava::tui::action_name(ava::tui::TuiAction::SelectNext) == "select_next" &&
          ava::tui::action_name(ava::tui::TuiAction::SelectPageUp) == "select_page_up" &&
          ava::tui::action_name(ava::tui::TuiAction::SelectPageDown) == "select_page_down" &&
          ava::tui::action_name(ava::tui::TuiAction::SelectConfirm) == "select_confirm" &&
          ava::tui::action_name(ava::tui::TuiAction::SelectCancel) == "select_cancel" &&
          ava::tui::action_name(ava::tui::TuiAction::CopySelection) == "copy_selection" &&
          ava::tui::action_name(ava::tui::TuiAction::ExternalEditor) == "external_editor" && ava::tui::action_name(ava::tui::TuiAction::Suspend) == "suspend" &&
          ava::tui::action_name(ava::tui::TuiAction::ClipboardPasteImage) == "clipboard_paste_image" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelSelect) == "model_select" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelCycleForward) == "model_cycle_forward" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelCycleBackward) == "model_cycle_backward" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelsSave) == "models_save" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelsEnableAll) == "models_enable_all" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelsClearAll) == "models_clear_all" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelsToggleProvider) == "models_toggle_provider" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelsReorderUp) == "models_reorder_up" &&
          ava::tui::action_name(ava::tui::TuiAction::ModelsReorderDown) == "models_reorder_down" &&
          ava::tui::action_name(ava::tui::TuiAction::ThinkingToggle) == "thinking_toggle" &&
          ava::tui::action_name(ava::tui::TuiAction::MessageFollowUp) == "message_follow_up" &&
          ava::tui::action_name(ava::tui::TuiAction::MessageDequeue) == "message_dequeue" &&
          ava::tui::action_name(ava::tui::TuiAction::MessagePrev) == "message_prev" &&
          ava::tui::action_name(ava::tui::TuiAction::MessageNext) == "message_next" &&
          ava::tui::action_name(ava::tui::TuiAction::JumpToBottom) == "jump_to_bottom" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionNew) == "session_new" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionTree) == "session_tree" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionFork) == "session_fork" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionResume) == "session_resume" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionTogglePath) == "session_toggle_path" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionToggleSort) == "session_toggle_sort" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionToggleNamedFilter) == "session_toggle_named_filter" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionRename) == "session_rename" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionArchive) == "session_archive" &&
          ava::tui::action_name(ava::tui::TuiAction::SessionArchiveNoninvasive) == "session_archive_noninvasive" &&
          ava::tui::action_name(ava::tui::TuiAction::TreeFoldOrUp) == "tree_fold_or_up" &&
          ava::tui::action_name(ava::tui::TuiAction::TreeUnfoldOrDown) == "tree_unfold_or_down" &&
          ava::tui::action_name(ava::tui::TuiAction::TreeEditLabel) == "tree_edit_label" &&
          ava::tui::action_name(ava::tui::TuiAction::TreeToggleLabelTimestamp) == "tree_toggle_label_timestamp",
      "tui keybind parser exposes semantic action names for editor and navigation actions");
  auto const help_items = ava::tui::key_binding_help_items(default_bindings);
  expect(
      std::ranges::any_of(help_items,
                          [](ava::tui::TuiKeyBindingHelpItem const& item) {
                            return item.action == "variant_cycle" && item.keys.find("Shift+Tab") != std::string::npos &&
                                   item.keys.find("Ctrl+T") == std::string::npos;
                          }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "thinking_toggle" && item.keys.find("Ctrl+T") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "model_select" && item.keys.find("Ctrl+L") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "copy_selection" && item.keys.find("Ctrl+C") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "clear_input" && item.keys.find("Ctrl+C") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "external_editor" && item.keys.find("Ctrl+G") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "suspend" && item.keys.find("Ctrl+Z") != std::string::npos; }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "clipboard_paste_image" && item.keys.find("Ctrl+V") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "model_cycle_forward" && item.keys.find("Ctrl+P") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "model_cycle_backward" && item.keys.find("Shift+Ctrl+P") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "models_clear_all" && item.keys.find("Ctrl+X") != std::string::npos; }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "message_follow_up" && item.keys.find("Alt+Enter") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "message_dequeue" && item.keys.find("Alt+Up") != std::string::npos; }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "session_toggle_path" && item.keys.find("Ctrl+P") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "session_toggle_sort" && item.keys.find("Ctrl+S") != std::string::npos &&
                                       item.keys.find("Ctrl+T") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "session_archive" && item.keys.find("Ctrl+D") != std::string::npos; }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "tree_fold_or_up" && item.keys.find("Ctrl+Left") != std::string::npos &&
                                       item.keys.find("Alt+Left") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "tree_unfold_or_down" && item.keys.find("Ctrl+Right") != std::string::npos &&
                                       item.keys.find("Alt+Right") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "delete_to_line_start" && item.keys.find("Ctrl+U") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "jump_forward" && item.keys.find("Ctrl+]") != std::string::npos; }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "jump_backward" && item.keys.find("Ctrl+Alt+]") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "delete_forward" && item.keys.find("Shift+Delete") != std::string::npos &&
                                       item.keys.find("Ctrl+D") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "delete_backward" && item.keys.find("Shift+Backspace") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "delete_word_backward" && item.keys.find("Alt+Backspace") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "delete_word_forward" && item.keys.find("Alt+D") != std::string::npos;
                              }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "delete_word_forward" && item.keys.find("Alt+Delete") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              help_items, [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "undo" && item.keys.find("Ctrl+Z") == std::string::npos; }) &&
          std::ranges::any_of(
              help_items, [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "undo" && item.keys.find("Ctrl+-") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items, [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "redo" && item.keys.find("Ctrl+R") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items, [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "yank" && item.keys.find("Ctrl+Y") != std::string::npos; }) &&
          std::ranges::any_of(
              help_items,
              [](ava::tui::TuiKeyBindingHelpItem const& item) { return item.action == "yank_pop" && item.keys.find("Alt+Y") != std::string::npos; }) &&
          std::ranges::any_of(help_items,
                              [](ava::tui::TuiKeyBindingHelpItem const& item) {
                                return item.action == "session_archive_noninvasive" && item.keys.find("Ctrl+Backspace") != std::string::npos;
                              }),
      "tui keybind help lists concrete semantic action names and effective keys");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":\"Hyper+Enter\"}"), "tui keybind parser rejects unknown key names");
  expect(!ava::tui::parse_key_bindings_json("{\"submt\":\"Enter\"}"), "tui keybind parser rejects unknown action names");
  auto const escaped_action_keybinds = ava::tui::parse_key_bindings_json("{\"\\u0073\\u0075\\u0062\\u006d\\u0069\\u0074\":\"Ctrl+T\"}");
  expect(escaped_action_keybinds && ava::tui::key_matches_action(*escaped_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlT),
         "tui keybind parser accepts JSON unicode escapes in action names");
  auto const space_action_keybinds = ava::tui::parse_key_bindings_json("{\"tui.input.submit\":\"Space\"}");
  expect(space_action_keybinds && ava::tui::key_matches_action(*space_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Space),
         "tui keybind parser accepts Pi-style Space key names for semantic bindings");
  auto const ctrl_space_action_keybinds = ava::tui::parse_key_bindings_json("{\"tui.input.submit\":\"Ctrl+Space\"}");
  expect(ctrl_space_action_keybinds && ava::tui::key_matches_action(*ctrl_space_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlSpace),
         "tui keybind parser accepts Pi-style Ctrl+Space key names for semantic bindings");
  auto const ctrl_slash_action_keybinds = ava::tui::parse_key_bindings_json("{\"tui.input.submit\":\"Ctrl+/\"}");
  expect(ctrl_slash_action_keybinds && ava::tui::key_matches_action(*ctrl_slash_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlSlash),
         "tui keybind parser accepts Pi-style Ctrl+/ key names for semantic bindings");
  auto const select_action_keybinds = ava::tui::parse_key_bindings_json(
      "{\"tui.select.confirm\":[\"Enter\",\"Space\"],\"tui.select.cancel\":[\"Escape\",\"Ctrl+W\"],"
      "\"tui.select.up\":\"Ctrl+P\",\"tui.select.down\":\"Ctrl+N\","
      "\"tui.select.pageUp\":\"Ctrl+O\",\"tui.select.pageDown\":\"Ctrl+Y\"}");
  expect(select_action_keybinds && ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectConfirm, ava::tui::Key::Enter) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectConfirm, ava::tui::Key::Space) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Enter) &&
             !ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Space) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectCancel, ava::tui::Key::Escape) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectCancel, ava::tui::Key::CtrlW) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::Cancel, ava::tui::Key::Escape) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::DeleteWordBackward, ava::tui::Key::CtrlW) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectPrev, ava::tui::Key::CtrlP) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectNext, ava::tui::Key::CtrlN) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectPageUp, ava::tui::Key::CtrlO) &&
             ava::tui::key_matches_action(*select_action_keybinds, ava::tui::TuiAction::SelectPageDown, ava::tui::Key::CtrlY),
         "tui keybind parser maps Pi select action ids to context-specific select-list actions");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":123}"), "tui keybind parser rejects non-string action values");
  auto const array_action_keybinds = ava::tui::parse_key_bindings_json("{\"cursor_line_start\":[\"Home\",\"Ctrl+A\"],\"message_dequeue\":[\"Alt+Up\"]}");
  expect(array_action_keybinds && ava::tui::key_matches_action(*array_action_keybinds, ava::tui::TuiAction::CursorLineStart, ava::tui::Key::Home) &&
             ava::tui::key_matches_action(*array_action_keybinds, ava::tui::TuiAction::CursorLineStart, ava::tui::Key::CtrlA) &&
             ava::tui::key_matches_action(*array_action_keybinds, ava::tui::TuiAction::MessageDequeue, ava::tui::Key::AltArrowUp),
         "tui keybind parser accepts Pi-style arrays of key names");
  auto const namespaced_action_keybinds = ava::tui::parse_key_bindings_json(
      "{\"tui.editor.cursorLineStart\":[\"Home\",\"Ctrl+A\"],\"app.message.dequeue\":[\"Alt+Up\"],"
      "\"app.message.followUp\":\"Alt+Enter\","
      "\"app.clear\":\"Ctrl+C\","
      "\"app.editor.external\":\"Ctrl+G\","
      "\"app.suspend\":\"Ctrl+Z\","
      "\"app.clipboard.pasteImage\":\"Ctrl+V\","
      "\"app.tree.editLabel\":\"Shift+L\","
      "\"app.tools.expand\":\"Ctrl+O\",\"tui.editor.deleteCharBackward\":[\"Backspace\",\"Ctrl+H\"]}");
  expect(namespaced_action_keybinds && ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::CursorLineStart, ava::tui::Key::Home) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::CursorLineStart, ava::tui::Key::CtrlA) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::MessageDequeue, ava::tui::Key::AltArrowUp) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::MessageFollowUp, ava::tui::Key::AltEnter) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::ClearInput, ava::tui::Key::CtrlC) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::ExternalEditor, ava::tui::Key::CtrlG) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::Suspend, ava::tui::Key::CtrlZ) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::ClipboardPasteImage, ava::tui::Key::CtrlV) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlO) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::TreeEditLabel, ava::tui::Key::ShiftL) &&
             ava::tui::key_matches_action(*namespaced_action_keybinds, ava::tui::TuiAction::DeleteBackward, ava::tui::Key::CtrlH),
         "tui keybind parser accepts matching Pi namespaced action ids and Ctrl+H");
  auto const vim_action_keybinds = ava::tui::parse_key_bindings_json(
      "{\"tui.editor.cursorLeft\":[\"Left\",\"Alt+H\"],\"tui.editor.cursorDown\":[\"Down\",\"Alt+J\"],"
      "\"tui.editor.cursorUp\":[\"Up\",\"Alt+K\"],\"tui.editor.cursorRight\":[\"Right\",\"Alt+L\"],"
      "\"tui.editor.cursorWordRight\":[\"Alt+Right\",\"Alt+W\"]}");
  expect(vim_action_keybinds && ava::tui::key_matches_action(*vim_action_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH) &&
             ava::tui::key_matches_action(*vim_action_keybinds, ava::tui::TuiAction::CursorDown, ava::tui::Key::AltJ) &&
             ava::tui::key_matches_action(*vim_action_keybinds, ava::tui::TuiAction::CursorUp, ava::tui::Key::AltK) &&
             ava::tui::key_matches_action(*vim_action_keybinds, ava::tui::TuiAction::CursorRight, ava::tui::Key::AltL) &&
             ava::tui::key_matches_action(*vim_action_keybinds, ava::tui::TuiAction::CursorWordRight, ava::tui::Key::AltW),
         "tui keybind parser accepts Pi Vim-style Alt+H/J/K/L and Alt+W cursor aliases");
  auto const legacy_action_keybinds = ava::tui::parse_key_bindings_json(
      "{\"cursorLineEnd\":[\"End\",\"Ctrl+E\"],\"expandTools\":\"Ctrl+O\","
      "\"toggleThinking\":\"Ctrl+T\",\"followUp\":\"Alt+Enter\"}");
  expect(legacy_action_keybinds && ava::tui::key_matches_action(*legacy_action_keybinds, ava::tui::TuiAction::CursorLineEnd, ava::tui::Key::End) &&
             ava::tui::key_matches_action(*legacy_action_keybinds, ava::tui::TuiAction::CursorLineEnd, ava::tui::Key::CtrlE) &&
             ava::tui::key_matches_action(*legacy_action_keybinds, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlO) &&
             ava::tui::key_matches_action(*legacy_action_keybinds, ava::tui::TuiAction::ThinkingToggle, ava::tui::Key::CtrlT) &&
             ava::tui::key_matches_action(*legacy_action_keybinds, ava::tui::TuiAction::MessageFollowUp, ava::tui::Key::AltEnter),
         "tui keybind parser accepts legacy camelCase action aliases where AVA has matching semantics");
  auto const thinking_toggle_keybinds = ava::tui::parse_key_bindings_json("{\"app.thinking.toggle\":\"Ctrl+T\"}");
  expect(thinking_toggle_keybinds && ava::tui::key_matches_action(*thinking_toggle_keybinds, ava::tui::TuiAction::ThinkingToggle, ava::tui::Key::CtrlT) &&
             !ava::tui::key_matches_action(*thinking_toggle_keybinds, ava::tui::TuiAction::VariantCycle, ava::tui::Key::CtrlT),
         "tui keybind parser accepts Pi thinking visibility toggle action id");
  auto const namespaced_over_legacy_keybinds =
      ava::tui::parse_key_bindings_json("{\"app.tools.expand\":\"Ctrl+O\",\"expandTools\":\"Ctrl+T\",\"variant_cycle\":\"Ctrl+T\"}");
  expect(namespaced_over_legacy_keybinds &&
             ava::tui::key_matches_action(*namespaced_over_legacy_keybinds, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlO) &&
             !ava::tui::key_matches_action(*namespaced_over_legacy_keybinds, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlT) &&
             ava::tui::key_matches_action(*namespaced_over_legacy_keybinds, ava::tui::TuiAction::VariantCycle, ava::tui::Key::CtrlT),
         "tui keybind parser gives namespaced action ids precedence over legacy aliases before conflict checks");
  auto const later_current_alias_keybinds = ava::tui::parse_key_bindings_json("{\"tui.input.submit\":\"Ctrl+T\",\"submit\":\"Ctrl+D\"}");
  expect(later_current_alias_keybinds && ava::tui::key_matches_action(*later_current_alias_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlD) &&
             !ava::tui::key_matches_action(*later_current_alias_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlT),
         "tui keybind parser lets the later current-form alias win for the same effective action");
  auto const copy_action_keybinds = ava::tui::parse_key_bindings_json("{\"tui.input.copy\":\"Ctrl+C\"}");
  expect(copy_action_keybinds && ava::tui::key_matches_action(*copy_action_keybinds, ava::tui::TuiAction::CopySelection, ava::tui::Key::CtrlC) &&
             ava::tui::key_matches_action(*copy_action_keybinds, ava::tui::TuiAction::ClearInput, ava::tui::Key::CtrlC) &&
             ava::tui::key_matches_action(*copy_action_keybinds, ava::tui::TuiAction::Interrupt, ava::tui::Key::CtrlC),
         "tui keybind parser accepts Pi copy action ids while preserving Ctrl+C clear and interrupt fallbacks");
  auto const session_delete_noninvasive_keybinds = ava::tui::parse_key_bindings_json("{\"app.session.deleteNoninvasive\":\"Ctrl+Backspace\"}");
  expect(session_delete_noninvasive_keybinds &&
             ava::tui::key_matches_action(*session_delete_noninvasive_keybinds, ava::tui::TuiAction::SessionArchiveNoninvasive, ava::tui::Key::CtrlBackspace),
         "tui keybind parser accepts Pi session non-invasive delete action id");
  auto const session_action_keybinds = ava::tui::parse_key_bindings_json(
      "{\"app.session.new\":\"Alt+H\",\"app.session.tree\":\"Alt+J\","
      "\"app.session.fork\":\"Alt+K\",\"app.session.resume\":\"Alt+L\","
      "\"app.session.togglePath\":\"Ctrl+O\","
      "\"app.session.toggleSort\":\"Ctrl+Y\","
      "\"app.session.toggleNamedFilter\":\"Ctrl+U\","
      "\"app.session.rename\":\"Ctrl+K\","
      "\"app.session.delete\":\"Alt+D\"}");
  expect(session_action_keybinds && ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionNew, ava::tui::Key::AltH) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionTree, ava::tui::Key::AltJ) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionFork, ava::tui::Key::AltK) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionResume, ava::tui::Key::AltL) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionTogglePath, ava::tui::Key::CtrlO) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionToggleSort, ava::tui::Key::CtrlY) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionToggleNamedFilter, ava::tui::Key::CtrlU) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionRename, ava::tui::Key::CtrlK) &&
             ava::tui::key_matches_action(*session_action_keybinds, ava::tui::TuiAction::SessionArchive, ava::tui::Key::AltD),
         "tui keybind parser accepts Pi session action ids");
  auto const tree_action_keybinds = ava::tui::parse_key_bindings_json(
      "{\"app.tree.foldOrUp\":\"Ctrl+O\","
      "\"app.tree.unfoldOrDown\":\"Ctrl+Y\","
      "\"app.tree.editLabel\":\"Shift+L\","
      "\"app.tree.toggleLabelTimestamp\":\"Shift+T\","
      "\"app.tree.filter.labeledOnly\":\"Ctrl+Space\","
      "\"app.tree.filter.all\":\"Ctrl+/\"}");
  expect(tree_action_keybinds && ava::tui::key_matches_action(*tree_action_keybinds, ava::tui::TuiAction::TreeFoldOrUp, ava::tui::Key::CtrlO) &&
             ava::tui::key_matches_action(*tree_action_keybinds, ava::tui::TuiAction::TreeUnfoldOrDown, ava::tui::Key::CtrlY) &&
             ava::tui::key_matches_action(*tree_action_keybinds, ava::tui::TuiAction::TreeEditLabel, ava::tui::Key::ShiftL) &&
             ava::tui::key_matches_action(*tree_action_keybinds, ava::tui::TuiAction::TreeToggleLabelTimestamp, ava::tui::Key::ShiftT) &&
             ava::tui::key_matches_action(*tree_action_keybinds, ava::tui::TuiAction::TreeFilterLabeledOnly, ava::tui::Key::CtrlSpace) &&
             ava::tui::key_matches_action(*tree_action_keybinds, ava::tui::TuiAction::TreeFilterAll, ava::tui::Key::CtrlSlash),
         "tui keybind parser accepts Pi tree branch navigation, label, and equivalent filter action ids");
  expect(!ava::tui::parse_key_bindings_json("{\"app.tree.filter.noTools\":\"Ctrl+T\"}"),
         "tui keybind parser rejects Pi tree filter ids without an AVA session-selector equivalent");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":[]}"), "tui keybind parser rejects empty keybinding arrays");
  expect(!ava::tui::parse_key_bindings_json("{\"submit\":[\"Enter\",123]}"), "tui keybind parser rejects non-string keybinding array entries");
  auto const conflicting_keybinds = ava::tui::parse_key_bindings_json("{\"model_cycle_forward\":\"Ctrl+P\",\"model_cycle_backward\":\"Ctrl+P\"}");
  auto const conflicting_keybinds_error = conflicting_keybinds ? std::string() : conflicting_keybinds.error().format();
  expect(!conflicting_keybinds && conflicting_keybinds_error.find("conflicting TUI keybinding") != std::string::npos &&
             conflicting_keybinds_error.find("Ctrl+P") != std::string::npos && conflicting_keybinds_error.find("model_cycle_forward") != std::string::npos &&
             conflicting_keybinds_error.find("model_cycle_backward") != std::string::npos,
         "tui keybind parser rejects user-configured conflicts with actionable key and action context");
  auto const conflicting_select_keybinds = ava::tui::parse_key_bindings_json("{\"tui.select.confirm\":\"Space\",\"tui.select.cancel\":\"Space\"}");
  auto const conflicting_select_keybinds_error = conflicting_select_keybinds ? std::string() : conflicting_select_keybinds.error().format();
  expect(!conflicting_select_keybinds && conflicting_select_keybinds_error.find("conflicting TUI keybinding") != std::string::npos &&
             conflicting_select_keybinds_error.find("Space") != std::string::npos &&
             conflicting_select_keybinds_error.find("select_confirm") != std::string::npos &&
             conflicting_select_keybinds_error.find("select_cancel") != std::string::npos,
         "tui keybind parser rejects conflicts within the select-list keybinding context");
  auto const shadowing_keybinds = ava::tui::parse_key_bindings_json("{\"submit\":\"Ctrl+D\"}");
  expect(shadowing_keybinds && ava::tui::key_matches_action(*shadowing_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlD) &&
             !ava::tui::key_matches_action(*shadowing_keybinds, ava::tui::TuiAction::DeleteForward, ava::tui::Key::CtrlD) &&
             !ava::tui::key_matches_action(*shadowing_keybinds, ava::tui::TuiAction::Exit, ava::tui::Key::CtrlD),
         "tui keybind parser lets custom bindings shadow shared default keys without flagging conflicts");

  auto const default_config_json = ava::tui::default_key_bindings_config_json();
  auto const default_config_keybinds = ava::tui::parse_key_bindings_json(default_config_json);
  expect(
      default_config_json.find("\"tui.editor.cursorLeft\"") != std::string::npos && default_config_json.find("\"app.clear\"") != std::string::npos &&
          default_config_json.find("\"tui.input.copy\"") != std::string::npos && default_config_json.find("\"app.editor.external\"") != std::string::npos &&
          default_config_json.find("\"app.suspend\"") != std::string::npos && default_config_json.find("\"app.clipboard.pasteImage\"") != std::string::npos &&
          default_config_json.find("\"app.session.togglePath\"") != std::string::npos &&
          default_config_json.find("\"app.session.toggleSort\"") != std::string::npos &&
          default_config_json.find("\"app.session.toggleNamedFilter\"") != std::string::npos &&
          default_config_json.find("\"app.session.rename\"") != std::string::npos && default_config_json.find("\"app.session.delete\"") != std::string::npos &&
          default_config_json.find("\"app.session.deleteNoninvasive\"") != std::string::npos &&
          default_config_json.find("\"app.tree.foldOrUp\"") != std::string::npos &&
          default_config_json.find("\"app.tree.unfoldOrDown\"") != std::string::npos &&
          default_config_json.find("\"app.tree.editLabel\"") != std::string::npos &&
          default_config_json.find("\"app.tree.toggleLabelTimestamp\"") != std::string::npos &&
          default_config_json.find("\"app.tree.filter.labeledOnly\"") == std::string::npos &&
          default_config_json.find("\"app.tree.filter.all\"") == std::string::npos &&
          default_config_json.find("\"app.models.clearAll\"") != std::string::npos &&
          default_config_json.find("\"app.thinking.toggle\"") != std::string::npos &&
          default_config_json.find("\"app.message.followUp\"") != std::string::npos &&
          default_config_json.find("\"app.model.cycleForward\"") != std::string::npos && default_config_json.find("\"history_prev\"") == std::string::npos &&
          default_config_json.find("\"mode_toggle\"") == std::string::npos && default_config_keybinds &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::CtrlB) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::CopySelection, ava::tui::Key::CtrlC) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::ClearInput, ava::tui::Key::CtrlC) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::ExternalEditor, ava::tui::Key::CtrlG) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::Suspend, ava::tui::Key::CtrlZ) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::ClipboardPasteImage, ava::tui::Key::CtrlV) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::Interrupt, ava::tui::Key::CtrlC) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::SessionTogglePath, ava::tui::Key::CtrlP) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::SessionToggleSort, ava::tui::Key::CtrlS) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::SessionToggleNamedFilter, ava::tui::Key::CtrlN) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::SessionRename, ava::tui::Key::CtrlR) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::SessionArchive, ava::tui::Key::CtrlD) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::SessionArchiveNoninvasive, ava::tui::Key::CtrlBackspace) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::TreeFoldOrUp, ava::tui::Key::CtrlArrowLeft) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::TreeUnfoldOrDown, ava::tui::Key::CtrlArrowRight) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::TreeEditLabel, ava::tui::Key::ShiftL) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::TreeToggleLabelTimestamp, ava::tui::Key::ShiftT) &&
          !ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::TreeFilterLabeledOnly, ava::tui::Key::CtrlN) &&
          !ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::TreeFilterAll, ava::tui::Key::CtrlA) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::ModelsClearAll, ava::tui::Key::CtrlX) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::ThinkingToggle, ava::tui::Key::CtrlT) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::MessageFollowUp, ava::tui::Key::AltEnter) &&
          ava::tui::key_matches_action(*default_config_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Enter),
      "tui keybind default config template uses Pi-style ids and remains valid with intentional shared defaults");

  auto const root = create_empty_root("tui-keybinds");
  auto const keybind_root = root / "tui-keybinds";
  std::filesystem::remove_all(keybind_root);
  std::filesystem::create_directories(keybind_root);
  auto const keybinds_file = keybind_root / "keybinds.json";
  auto const missing_keybinds = ava::tui::load_key_bindings(keybinds_file);
  expect(missing_keybinds && ava::tui::key_matches_action(*missing_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Enter),
         "tui keybind file loader falls back to defaults when the file is missing");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submit\":\"Ctrl+T\",\"variant_cycle\":[\"Shift+Tab\",\"Ctrl+D\"]}";
  }
  auto const loaded_keybinds = ava::tui::load_key_bindings(keybinds_file);
  expect(loaded_keybinds && ava::tui::key_matches_action(*loaded_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::CtrlT) &&
             ava::tui::key_matches_action(*loaded_keybinds, ava::tui::TuiAction::VariantCycle, ava::tui::Key::ShiftTab) &&
             ava::tui::key_matches_action(*loaded_keybinds, ava::tui::TuiAction::VariantCycle, ava::tui::Key::CtrlD),
         "tui keybind file loader reads valid configured string and array bindings");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submit\":\"Enter\"";
  }
  expect(!ava::tui::load_key_bindings(keybinds_file), "tui keybind file loader rejects malformed JSON");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submt\":\"Enter\"}";
  }
  expect(!ava::tui::load_key_bindings(keybinds_file), "tui keybind file loader rejects unknown actions");
  {
    std::ofstream output(keybinds_file);
    output << "{\"submit\":\"Ctrl+P\",\"model_cycle_forward\":\"Ctrl+P\"}";
  }
  auto const conflicting_loaded_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const conflicting_loaded_keybinds_error = conflicting_loaded_keybinds ? std::string() : conflicting_loaded_keybinds.error().format();
  expect(!conflicting_loaded_keybinds && conflicting_loaded_keybinds_error.find("conflicting TUI keybinding") != std::string::npos &&
             conflicting_loaded_keybinds_error.find("Ctrl+P") != std::string::npos &&
             conflicting_loaded_keybinds_error.find(keybinds_file.string()) != std::string::npos,
         "tui keybind file loader reports configured conflicts with file path context");

  std::vector<ava::tui::SlashCommandItem> const disabled_slash_commands = {
      ava::tui::SlashCommandItem{.command = "/models",
                                 .description = "Select model",
                                 .category = "Planned",
                                 .aliases = {"/model"},
                                 .key_display = "Ctrl+M",
                                 .enabled = false,
                                 .disabled_reason = "model switching is not implemented"},
      ava::tui::SlashCommandItem{.command = "/mode", .description = "Toggle mode", .category = "General"}};
  auto const alias_matches = ava::tui::filter_slash_commands("/model", disabled_slash_commands);
  expect(alias_matches.size() == 1 && alias_matches.front().command == "/models", "tui slash palette filters aliases as well as primary command names");
  expect(ava::tui::slash_palette_visible("/model", disabled_slash_commands), "tui slash palette keeps disabled exact alias matches visible");
  auto const disabled_reason = ava::tui::slash_command_selection_disabled_reason("/model", disabled_slash_commands, 0);
  expect(disabled_reason && disabled_reason->find("not implemented") != std::string::npos, "tui slash selection exposes disabled command explanations");

  auto const disabled_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "/model",
                                                                                     .status = "ready",
                                                                                     .transcript = {},
                                                                                     .slash_commands = disabled_slash_commands,
                                                                                     .selected_slash_command_index = 0,
                                                                                     .width = 140,
                                                                                     .height = 10});
  expect(std::ranges::any_of(disabled_palette,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("/models (/model)") != std::string::npos && visible.find("Ctrl+M") != std::string::npos &&
                                      visible.find("disabled: model switching is not implemented") != std::string::npos;
                             }),
         "tui slash palette renders aliases, key displays, and disabled reasons");

  auto const palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                            .provider = "openai",
                                                                            .model = "gpt-5.5",
                                                                            .session_id = "session_test",
                                                                            .input = "/g",
                                                                            .status = "ready",
                                                                            .transcript = {},
                                                                            .slash_commands = slash_commands,
                                                                            .selected_slash_command_index = 1,
                                                                            .width = 80,
                                                                            .height = 12});
  expect(
      std::ranges::any_of(palette, [](std::string const& line) { return line.find("commands matching /g") == std::string::npos; }) &&
          std::ranges::any_of(palette,
                              [](std::string const& line) {
                                return line.find("/grep") != std::string::npos && line.find("Files") != std::string::npos &&
                                       line.find("Search files") != std::string::npos;
                              }) &&
          std::ranges::any_of(
              palette,
              [](std::string const& line) { return line.find("/glob") != std::string::npos && line.find("List matching files") != std::string::npos; }) &&
          std::ranges::any_of(palette,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("› /glob") != std::string::npos && visible.find("selected") == std::string::npos &&
                                       visible.find("(2/2)") == std::string::npos;
                              }) &&
          std::ranges::any_of(
              palette, [](std::string const& line) { return line.find("\x1b[7m› /glob") != std::string::npos && line.find("\x1b[0m") != std::string::npos; }) &&
          std::ranges::none_of(palette, [](std::string const& line) { return line.find("/help") != std::string::npos; }),
      "tui renders filtered slash-command palette with composer-integrated selected item highlight");
  auto const suppressed_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                       .provider = "openai",
                                                                                       .model = "gpt-5.5",
                                                                                       .session_id = "session_test",
                                                                                       .input = "/g",
                                                                                       .status = "ready",
                                                                                       .transcript = {},
                                                                                       .slash_commands = slash_commands,
                                                                                       .slash_palette_suppressed = true,
                                                                                       .width = 80,
                                                                                       .height = 12});
  expect(std::ranges::none_of(suppressed_palette, [](std::string const& line) { return strip_sgr(line).find("/grep") != std::string::npos; }) &&
             std::ranges::any_of(suppressed_palette, [](std::string const& line) { return strip_sgr(line).find("│  /g") != std::string::npos; }),
         "tui can dismiss slash autocomplete without clearing the draft input");
  auto const clicked_palette_index = ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                                 .provider = "openai",
                                                                                                                 .model = "gpt-5.5",
                                                                                                                 .session_id = "session_test",
                                                                                                                 .input = "/g",
                                                                                                                 .status = "ready",
                                                                                                                 .transcript = {},
                                                                                                                 .slash_commands = slash_commands,
                                                                                                                 .selected_slash_command_index = 1,
                                                                                                                 .width = 80,
                                                                                                                 .height = 12},
                                                                                      10);
  expect(clicked_palette_index && *clicked_palette_index == 1, "tui maps slash palette screen rows back to selectable commands for clicks");
  auto const suppressed_clicked_palette_index = ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                                            .provider = "openai",
                                                                                                                            .model = "gpt-5.5",
                                                                                                                            .session_id = "session_test",
                                                                                                                            .input = "/g",
                                                                                                                            .status = "ready",
                                                                                                                            .transcript = {},
                                                                                                                            .slash_commands = slash_commands,
                                                                                                                            .selected_slash_command_index = 1,
                                                                                                                            .slash_palette_suppressed = true,
                                                                                                                            .width = 80,
                                                                                                                            .height = 12},
                                                                                                 9);
  expect(!suppressed_clicked_palette_index, "tui ignores slash palette click mapping after autocomplete is dismissed");
  auto const blocked_question_palette_index =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/g",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = slash_commands,
                                                                                  .question_prompt = ava::tui::QuestionPromptView{.header = "Question",
                                                                                                                                  .question = "Choose",
                                                                                                                                  .options = {},
                                                                                                                                  .multiple = false,
                                                                                                                                  .allow_custom = false,
                                                                                                                                  .selected_option_index = 0,
                                                                                                                                  .custom_text = ""},
                                                                                  .selected_slash_command_index = 1,
                                                                                  .width = 80,
                                                                                  .height = 12},
                                                       9);
  expect(!blocked_question_palette_index, "tui ignores slash palette click mapping while a question prompt is active");

  std::vector<ava::tui::SlashCommandItem> many_slash_commands;
  for (int index = 0; index < 8; ++index)
  {
    many_slash_commands.push_back(ava::tui::SlashCommandItem{.command = "/item" + std::to_string(index), .description = "Command " + std::to_string(index)});
  }
  auto const tiny_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                 .provider = "openai",
                                                                                 .model = "gpt-5.5",
                                                                                 .session_id = "session_test",
                                                                                 .input = "/",
                                                                                 .status = "ready",
                                                                                 .transcript = {},
                                                                                 .slash_commands = many_slash_commands,
                                                                                 .selected_slash_command_index = 6,
                                                                                 .width = 80,
                                                                                 .height = 8});
  expect(std::ranges::any_of(tiny_palette, [](std::string const& line) { return line.find("› /item6") != std::string::npos; }) &&
             std::ranges::none_of(tiny_palette, [](std::string const& line) { return line.find("/item0") != std::string::npos; }),
         "tui keeps selected slash palette item visible when height is tight");
  auto const first_scrolled_palette_click = ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                                        .provider = "openai",
                                                                                                                        .model = "gpt-5.5",
                                                                                                                        .session_id = "session_test",
                                                                                                                        .input = "/",
                                                                                                                        .status = "ready",
                                                                                                                        .transcript = {},
                                                                                                                        .slash_commands = many_slash_commands,
                                                                                                                        .selected_slash_command_index = 6,
                                                                                                                        .width = 80,
                                                                                                                        .height = 8},
                                                                                             1);
  auto const selected_scrolled_palette_click =
      ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "/",
                                                                                  .status = "ready",
                                                                                  .transcript = {},
                                                                                  .slash_commands = many_slash_commands,
                                                                                  .selected_slash_command_index = 6,
                                                                                  .width = 80,
                                                                                  .height = 8},
                                                       6);
  auto const outside_scrolled_palette_click = ava::tui::slash_palette_selection_for_screen_row(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                                          .provider = "openai",
                                                                                                                          .model = "gpt-5.5",
                                                                                                                          .session_id = "session_test",
                                                                                                                          .input = "/",
                                                                                                                          .status = "ready",
                                                                                                                          .transcript = {},
                                                                                                                          .slash_commands = many_slash_commands,
                                                                                                                          .selected_slash_command_index = 6,
                                                                                                                          .width = 80,
                                                                                                                          .height = 8},
                                                                                               7);
  expect(first_scrolled_palette_click && *first_scrolled_palette_click == 1 && selected_scrolled_palette_click && *selected_scrolled_palette_click == 6 &&
             !outside_scrolled_palette_click,
         "tui maps slash palette click rows through a scrolled visible window and ignores outside rows");

  auto const starved_palette =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "/",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "must not leak"}},
                                                           .slash_commands = many_slash_commands,
                                                           .selected_slash_command_index = 4,
                                                           .width = 40,
                                                           .height = 8});
  expect(starved_palette.size() == 8 &&
             std::ranges::any_of(starved_palette, [](std::string const& line) { return strip_sgr(line).find("› /item4") != std::string::npos; }) &&
             std::ranges::none_of(starved_palette, [](std::string const& line) { return strip_sgr(line).find("must not leak") != std::string::npos; }),
         "tui keeps the bottom composer fixed when the slash palette exhausts transcript height");

  auto const no_match_palette = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "/zz",
                                                                                     .status = "ready",
                                                                                     .transcript = {},
                                                                                     .slash_commands = slash_commands,
                                                                                     .width = 80,
                                                                                     .height = 12});
  expect(std::ranges::any_of(no_match_palette, [](std::string const& line) { return strip_sgr(line).find("no commands match /zz") != std::string::npos; }),
         "tui slash-command palette renders deterministic empty state");

  auto const permission_modal =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "permission required",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "thinking"}},
                                                           .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash\x1b[31m",
                                                                                                               .operation = "bash",
                                                                                                               .target = "/tmp/outside",
                                                                                                               .command = "git push origin main",
                                                                                                               .reason = "command can change external state",
                                                                                                               .risk = "high",
                                                                                                               .request_id = "permreq_push"},
                                                           .width = 80,
                                                           .height = 12});
  expect(std::ranges::any_of(permission_modal, [](std::string const& line) { return line.find("Permission required") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("git push origin main") != std::string::npos;
                                 }) &&
             std::ranges::any_of(
                 permission_modal,
                 [](std::string const& line) { return line.find("[Reject]") != std::string::npos && line.find("[Allow once]") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   return line.find("\x1b[7m> [Reject]") != std::string::npos && strip_sgr(line).find("(selected)") == std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("A allow") != std::string::npos && visible.find("D reject") != std::string::npos &&
                                          visible.find("Enter confirm") != std::string::npos && visible.find("Esc reject") != std::string::npos;
                                 }) &&
             std::ranges::any_of(permission_modal,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("risk high") != std::string::npos && visible.find("id permreq_push") != std::string::npos &&
                                          visible.find("reason command can change external state") != std::string::npos;
                                 }) &&
             std::ranges::none_of(permission_modal,
                                  [](std::string const& line) { return line.find("bash") != std::string::npos && line.find("\x1b[31m") != std::string::npos; }),
         "tui renders OpenCode-aligned permission dock with default reject focus");
  expect(std::ranges::all_of(permission_modal, [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 80; }) &&
             std::ranges::any_of(permission_modal, [](std::string const& line) { return strip_sgr(line).find("[Reject]") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal, [](std::string const& line) { return strip_sgr(line).find("[Allow once]") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal, [](std::string const& line) { return strip_sgr(line).find("Enter confirm") != std::string::npos; }) &&
             std::ranges::any_of(permission_modal, [](std::string const& line) { return strip_sgr(line).find("Esc reject") != std::string::npos; }),
         "tui permission dock controls stay within 80 visible columns without losing controls");

  auto const external_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt =
          ava::tui::PermissionPromptView{
              .tool_name = "read_file", .operation = "read", .target = "/tmp/outside.txt", .reason = "target is outside the workspace"},
      .width = 80,
      .height = 8});
  expect(std::ranges::any_of(external_permission_modal,
                             [](std::string const& line) { return strip_sgr(line).find("Access external directory /tmp/outside.txt") != std::string::npos; }),
         "tui permission dock uses OpenCode-style external-directory wording for outside-workspace targets");

  auto const remembered_permission_modal =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "permission required",
                                                           .transcript = {},
                                                           .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash",
                                                                                                               .operation = "bash",
                                                                                                               .target = "/workspace",
                                                                                                               .command = "git push origin main",
                                                                                                               .reason = "command can change external state",
                                                                                                               .risk = "high",
                                                                                                               .allow_remember_available = true,
                                                                                                               .deny_remember_available = true},
                                                           .width = 96,
                                                           .height = 10});
  expect(
      std::ranges::any_of(remembered_permission_modal,
                          [](std::string const& line) {
                            auto visible = strip_sgr(line);
                            return visible.find("[Reject rule]") != std::string::npos && visible.find("[Always in this project]") != std::string::npos;
                          }) &&
          std::ranges::any_of(remembered_permission_modal, [](std::string const& line) { return strip_sgr(line).find("R remember") != std::string::npos; }) &&
          std::ranges::all_of(remembered_permission_modal, [](std::string const& line) { return visible_columns(line) <= 96; }),
      "tui permission dock exposes remembered reject and always-allow choices when rule storage is available");

  auto const session_permission_modal =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "permission required",
                                                           .transcript = {},
                                                           .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash",
                                                                                                               .operation = "bash",
                                                                                                               .target = "/workspace",
                                                                                                               .command = "cargo test",
                                                                                                               .reason = "sealed workspace recipe",
                                                                                                               .risk = "medium",
                                                                                                               .allow_session_available = true,
                                                                                                               .allow_remember_available = true,
                                                                                                               .deny_remember_available = true},
                                                           .width = 120,
                                                           .height = 10});
  expect(std::ranges::any_of(session_permission_modal,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[Allow session]") != std::string::npos && visible.find("[Allow once]") != std::string::npos &&
                                      visible.find("[Always in this project]") != std::string::npos && visible.find("[Reject rule]") != std::string::npos;
                             }) &&
             std::ranges::any_of(session_permission_modal,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("S allow session") != std::string::npos && visible.find("R remember") != std::string::npos;
                                 }) &&
             std::ranges::all_of(session_permission_modal, [](std::string const& line) { return visible_columns(line) <= 120; }),
         "tui permission dock exposes allow session between allow once and always-allow when session grant is available");

  auto const deny_only_remember_permission_modal =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "permission required",
                                                           .transcript = {},
                                                           .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash",
                                                                                                               .operation = "bash",
                                                                                                               .target = "/workspace",
                                                                                                               .command = "curl https://example.test",
                                                                                                               .reason = "containment unavailable",
                                                                                                               .risk = "critical",
                                                                                                               .allow_remember_available = false,
                                                                                                               .deny_remember_available = true},
                                                           .width = 96,
                                                           .height = 10});
  expect(std::ranges::any_of(deny_only_remember_permission_modal,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[Reject rule]") != std::string::npos && visible.find("[Always in this project]") == std::string::npos;
                             }) &&
             std::ranges::any_of(deny_only_remember_permission_modal,
                                 [](std::string const& line) { return strip_sgr(line).find("R remember") != std::string::npos; }),
         "tui permission dock renders a persistent deny without offering an unavailable persistent allow");

  auto const allow_focused_modal = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "permission required",
                                 .transcript = {},
                                 .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash",
                                                                                     .operation = "bash",
                                                                                     .target = "",
                                                                                     .command = "true",
                                                                                     .reason = "unknown risk",
                                                                                     .selected_choice = ava::tui::PermissionPromptChoice::Allow},
                                 .width = 80,
                                 .height = 14});
  expect(std::ranges::any_of(allow_focused_modal,
                             [](std::string const& line) {
                               return line.find("\x1b[7m> [Allow once]") != std::string::npos && strip_sgr(line).find("(selected)") == std::string::npos;
                             }),
         "tui permission dock highlights the selected allow choice");

  auto const diff_permission_modal = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "permission required",
                                 .transcript = {},
                                 .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "write_file",
                                                                                     .operation = "write_file",
                                                                                     .target = "/tmp/outside.txt",
                                                                                     .command = "",
                                                                                     .reason = "external mutation",
                                                                                     .diff_preview = "--- /tmp/outside.txt\n+++ /tmp/outside.txt\n@@ -1,1 +1,1 "
                                                                                                     "@@\n-old line\n+new line\n",
                                                                                     .diff_truncated = true},
                                 .width = 54,
                                 .height = 15});
  expect(
      std::ranges::all_of(diff_permission_modal, [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 54; }) &&
          std::ranges::any_of(diff_permission_modal, [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
          std::ranges::any_of(diff_permission_modal, [](std::string const& line) { return strip_sgr(line).find("-old line") != std::string::npos; }) &&
          std::ranges::any_of(diff_permission_modal, [](std::string const& line) { return strip_sgr(line).find("+new line") != std::string::npos; }) &&
          std::ranges::any_of(diff_permission_modal, [](std::string const& line) { return strip_sgr(line).find("[diff truncated]") != std::string::npos; }) &&
          std::ranges::any_of(diff_permission_modal,
                              [](std::string const& line) {
                                return strip_sgr(line).find("[Reject]") != std::string::npos && strip_sgr(line).find("[Allow once]") != std::string::npos;
                              }) &&
          std::ranges::any_of(diff_permission_modal, [](std::string const& line) { return strip_sgr(line).find("Esc reject") != std::string::npos; }),
      "tui permission dock renders backend-provided mutation diffs while preserving fail-closed controls");

  auto const narrow_diff_permission_modal = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "permission required",
                                 .transcript = {},
                                 .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "edit",
                                                                                     .operation = "edit",
                                                                                     .target = "/tmp/outside.txt",
                                                                                     .command = "",
                                                                                     .reason = "external mutation",
                                                                                     .diff_preview = "--- old\n+++ new\n@@ -1,1 +1,1 @@\n-old\n+new\n"},
                                 .width = 28,
                                 .height = 10});
  expect(std::ranges::all_of(narrow_diff_permission_modal,
                             [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 28; }) &&
             std::ranges::any_of(narrow_diff_permission_modal, [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
             std::ranges::any_of(narrow_diff_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("[Reject]") != std::string::npos && visible.find("[Allow once]") != std::string::npos;
                                 }),
         "tui permission dock keeps diff previews bounded at narrow widths");

  auto const long_permission_modal =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "permission required",
                                                           .transcript = {},
                                                           .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "",
                                                                                                               .operation = "write_file",
                                                                                                               .target = std::string(120, 't'),
                                                                                                               .command = std::string(120, 'c'),
                                                                                                               .reason = std::string(120, 'r'),
                                                                                                               .risk = "critical"},
                                                           .width = 80,
                                                           .height = 10});
  expect(
      std::ranges::all_of(long_permission_modal, [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 80; }) &&
          std::ranges::any_of(long_permission_modal,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("cccc") != std::string::npos && visible.find("...") != std::string::npos;
                              }) &&
          std::ranges::any_of(long_permission_modal,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("risk critical") != std::string::npos && visible.find("reason rrrr") != std::string::npos;
                              }),
      "tui permission dock keeps risk and reason visible when detail text is truncated");

  auto const tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "thinking"}},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 36,
      .height = 8});
  expect(std::ranges::any_of(tight_permission_modal, [](std::string const& line) { return line.find("Permission required") != std::string::npos; }) &&
             tight_permission_modal.size() <= 8 &&
             std::ranges::all_of(tight_permission_modal,
                                 [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 36; }) &&
             std::ranges::any_of(
                 tight_permission_modal,
                 [](std::string const& line) { return line.find("[Reject]") != std::string::npos && line.find("[Allow once]") != std::string::npos; }),
         "tui permission dock keeps header and controls visible in tight height");

  auto const ultra_tight_permission_modal = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "permission required",
      .transcript = {},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 20,
      .height = 8});
  expect(std::ranges::all_of(ultra_tight_permission_modal,
                             [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 20; }) &&
             ultra_tight_permission_modal.size() <= 8 &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("Permission") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("> [D]") != std::string::npos && visible.find("sel") == std::string::npos &&
                                          visible.find("[A]") != std::string::npos;
                                 }) &&
             std::ranges::any_of(ultra_tight_permission_modal,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("A=allow") != std::string::npos && visible.find("D=reject") != std::string::npos;
                                 }),
         "tui permission dock preserves reject and allow choices at minimum width");

  std::vector<ava::tui::TranscriptItem> permission_overflow_items;
  for (int index = 0; index < 8; ++index)
  {
    permission_overflow_items.push_back(ava::tui::TranscriptItem{.label = "ava", .text = "permission item " + std::to_string(index)});
  }
  auto const permission_starved = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "hidden input",
      .status = "permission required",
      .transcript = permission_overflow_items,
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 40,
      .height = 8});
  expect(permission_starved.size() <= 8 && std::ranges::all_of(permission_starved, [](std::string const& line) { return visible_columns(line) <= 40; }) &&
             std::ranges::none_of(permission_starved, [](std::string const& line) { return strip_sgr(line).find("lines hidden") != std::string::npos; }) &&
             std::ranges::any_of(permission_starved, [](std::string const& line) { return strip_sgr(line).find("│  hidden input") != std::string::npos; }),
         "tui permission prompt stays above the composer without hidden-line banners");

  auto const sanitized =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "bad\x1b[31mstatus",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "bad\x1b[31mred"}},
                                                           .width = 80,
                                                           .height = 8});
  expect(std::ranges::any_of(sanitized,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("?[31mred") != std::string::npos;
                             }),
         "tui render sanitizes transcript escape bytes in user content");
  auto const sanitized_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "bad\x1b[31mred",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 80,
                                                                                    .height = 8});
  expect(std::ranges::any_of(sanitized_input, [](std::string const& line) { return strip_sgr(line).find("│  bad?[31mred") != std::string::npos; }),
         "tui render sanitizes composer input escape bytes");
  expect(ava::tui::sanitize_terminal_text(std::string("osc") + static_cast<char>(0x9D) + "payload") == "osc?payload",
         "tui sanitizes raw c1 terminal control bytes");
  expect(ava::tui::sanitize_terminal_text("a\tb") == "a  b", "tui expands tabs before width accounting");
  expect(ava::tui::sanitize_terminal_text(std::string("ok ") + "\xC3\xA9") == std::string("ok ") + "\xC3\xA9", "tui sanitizer preserves valid utf-8 text");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xC0\x80", 2) + "y") == "x??y",
         "tui sanitizer rejects overlong two-byte utf-8 controls");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xE0\x80\x80", 3) + "y") == "x???y",
         "tui sanitizer rejects overlong three-byte utf-8 forms");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xF0\x80\x80\x80", 4) + "y") == "x????y",
         "tui sanitizer rejects overlong four-byte utf-8 forms");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xE2\x82", 2)) == "x??",
         "tui sanitizer replaces truncated utf-8 at the string boundary");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xED\xA0\x80", 3) + "y") == "x???y",
         "tui sanitizer rejects utf-8 surrogate codepoints");
  expect(ava::tui::sanitize_terminal_text(std::string("x") + std::string("\xF4\x90\x80\x80", 4) + "y") == "x????y",
         "tui sanitizer rejects utf-8 codepoints above the unicode maximum");
  expect(ava::tui::sanitize_terminal_text(std::string("nul") + std::string(1, '\0') + "byte") == "nul?byte",
         "tui sanitizer replaces binary-like NUL bytes with a visible marker");
  expect(ava::tui::detail::terminal_text_columns("\xE7\x95\x8C") == 2 && ava::tui::detail::terminal_text_columns(std::string("e") + "\xCC\x81") == 1 &&
             ava::tui::detail::terminal_text_columns(std::string("a") + "\xE2\x80\x8D" + "b") == 2 &&
             ava::tui::detail::terminal_text_columns(std::string("\xE2\x98\xBA") + "\xEF\xB8\x8F") >= 1,
         "tui width accounting handles CJK width and treats combining marks, zero-width joiners, and variation "
         "selectors as non-advancing");
  auto const regional_c = std::string("\xF0\x9F\x87\xA8");
  auto const regional_n = std::string("\xF0\x9F\x87\xB3");
  auto const thumbs_up = std::string("\xF0\x9F\x91\x8D");
  auto const light_skin_tone = std::string("\xF0\x9F\x8F\xBB");
  auto const man = std::string("\xF0\x9F\x91\xA8");
  auto const laptop = std::string("\xF0\x9F\x92\xBB");
  auto const zwj = std::string("\xE2\x80\x8D");
  auto const check_mark = std::string("\xE2\x9C\x85");
  auto const lightning = std::string("\xE2\x9A\xA1");
  auto const variation_16 = std::string("\xEF\xB8\x8F");
  auto const white_flag = std::string("\xF0\x9F\x8F\xB3");
  auto const rainbow = std::string("\xF0\x9F\x8C\x88");
  expect(ava::tui::detail::terminal_text_columns(regional_c) == 2 && ava::tui::detail::terminal_text_columns(regional_c + regional_n) == 2 &&
             ava::tui::detail::terminal_text_columns("      - " + regional_c) == 10 &&
             ava::tui::detail::terminal_text_columns(thumbs_up + light_skin_tone) == 2 && ava::tui::detail::terminal_text_columns(man + zwj + laptop) == 2 &&
             ava::tui::detail::terminal_text_columns(check_mark) == 2 && ava::tui::detail::terminal_text_columns(lightning) == 2 &&
             ava::tui::detail::terminal_text_columns(lightning + variation_16) == 2 &&
             ava::tui::detail::terminal_text_columns(white_flag + variation_16 + zwj + rainbow) == 2,
         "tui width accounting treats Pi-style regional indicators and emoji modifier/ZWJ clusters as stable wide cells");
  auto const partial_flag_wrap = ava::tui::detail::wrap_transcript_text("      - " + regional_c, 13);
  expect(partial_flag_wrap.size() == 2 && ava::tui::detail::terminal_text_columns(partial_flag_wrap[0]) <= 9 &&
             ava::tui::detail::terminal_text_columns(partial_flag_wrap[1]) == 2,
         "tui transcript wrapping breaks Pi-style partial-flag list lines before terminal overflow");
  auto const clipped_regional_indicator = ava::tui::detail::fit_line("x" + regional_c + "y", 2);
  expect(ava::tui::detail::terminal_text_columns(clipped_regional_indicator) <= 2, "tui narrow fitting does not undercount singleton regional indicators");
  auto const clipped_zwj_cluster = ava::tui::detail::fit_line(man + zwj + laptop + "x", 2);
  expect(clipped_zwj_cluster == man + zwj + laptop, "tui narrow fitting keeps emoji ZWJ clusters intact when they fit exactly");
  expect(ava::tui::detail::composer_input_prefix_columns(true) == 3 && ava::tui::detail::composer_input_prefix_columns(false) == 3,
         "tui composer input and continuation rows share one three-column boundary and gutter");
  auto const cursor_base = ava::tui::detail::composer_input_prefix_columns(true) + 1;
  auto const cursor_for = [](std::string input, std::size_t cursor) {
    return ava::tui::detail::input_cursor_column(ava::tui::ComposerSnapshot{.mode = "build",
                                                                            .provider = "openai",
                                                                            .model = "gpt-5.5",
                                                                            .session_id = "session_test",
                                                                            .input = std::move(input),
                                                                            .status = "ready",
                                                                            .transcript = {},
                                                                            .input_cursor = cursor},
                                                 120);
  };
  auto const cursor_text = std::string("a") + "\xE7\x95\x8C" + "e" + "\xCC\x81";
  expect(cursor_for(cursor_text, 1) == cursor_base + 1 && cursor_for(cursor_text, 4) == cursor_base + 3 && cursor_for(cursor_text, 5) == cursor_base + 4 &&
             cursor_for(cursor_text, cursor_text.size()) == cursor_base + 4 && cursor_for(std::string("x") + std::string("\xC0\x80", 2), 3) == cursor_base + 3,
         "tui composer cursor placement uses sanitized display columns for CJK, combining marks, and invalid utf-8");
  auto const wrapped_input = ava::tui::detail::input_render_line_spans("alpha beta gamma delta", 20);
  expect(wrapped_input.size() == 2 && wrapped_input[0].text == "alpha beta gamma " && wrapped_input[0].start == 0 &&
             wrapped_input[0].end == std::string("alpha beta gamma ").size() && wrapped_input[0].first_line && wrapped_input[1].text == "delta" &&
             wrapped_input[1].start == std::string("alpha beta gamma ").size() && !wrapped_input[1].first_line,
         "tui composer wraps long input at word boundaries while preserving source offsets");
  auto const wrapped_long_word = ava::tui::detail::input_render_line_spans("abcdefghijklmnopqr", 20);
  expect(wrapped_long_word.size() == 2 && wrapped_long_word[0].text == "abcdefghijklmnopq" && wrapped_long_word[1].text == "r",
         "tui composer falls back to cell-level wrapping for long unbroken input tokens");
  auto const cjk_wrap_text = std::string("\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C\xE7\x95\x8C");
  auto const wrapped_cjk_input = ava::tui::detail::input_render_line_spans(cjk_wrap_text, 20);
  expect(wrapped_cjk_input.size() == 2 && ava::tui::detail::terminal_text_columns(wrapped_cjk_input[0].text) == 16 &&
             ava::tui::detail::terminal_text_columns(wrapped_cjk_input[1].text) == 2,
         "tui composer wraps CJK input on full UTF-8 cell boundaries");
  auto const wrapped_cursor_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                  .provider = "openai",
                                                                  .model = "gpt-5.5",
                                                                  .session_id = "session_test",
                                                                  .input = "alpha beta gamma delta",
                                                                  .status = "ready",
                                                                  .transcript = {},
                                                                  .width = 20,
                                                                  .height = 8,
                                                                  .input_cursor = std::string::npos};
  expect(ava::tui::detail::input_cursor_line(wrapped_cursor_snapshot, 20) == 1 &&
             ava::tui::detail::input_cursor_column(wrapped_cursor_snapshot, 20) ==
                 ava::tui::detail::composer_input_prefix_columns(false) + std::string("delta").size() + 1,
         "tui composer places the cursor on the wrapped continuation row");
  auto const wrapped_render = ava::tui::render_composer(wrapped_cursor_snapshot);
  expect(std::ranges::any_of(wrapped_render, [](std::string const& line) { return strip_sgr(line).find("│  alpha beta gamma ") != std::string::npos; }) &&
             std::ranges::any_of(wrapped_render, [](std::string const& line) { return strip_sgr(line).find("│  delta") != std::string::npos; }),
         "tui composer renders wrapped input as visible continuation rows");
  auto const wrapped_click =
      ava::tui::composer_input_cursor_for_screen_position(wrapped_cursor_snapshot, 7, ava::tui::detail::composer_input_prefix_columns(false) + 3);
  expect(wrapped_click && *wrapped_click == std::string("alpha beta gamma de").size(),
         "tui composer hit-tests wrapped input continuation rows to source cursor offsets");
  auto const click_cursor_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                .provider = "openai",
                                                                .model = "gpt-5.5",
                                                                .session_id = "session_test",
                                                                .input = "alpha beta",
                                                                .status = "ready",
                                                                .transcript = {},
                                                                .width = 80,
                                                                .height = 8};
  auto const clicked_after_alpha = ava::tui::composer_input_cursor_for_screen_position(click_cursor_snapshot, 7, cursor_base + std::string("alpha ").size());
  expect(clicked_after_alpha && *clicked_after_alpha == std::string("alpha ").size() &&
             !ava::tui::composer_input_cursor_for_screen_position(click_cursor_snapshot, 1, cursor_base) &&
             !ava::tui::composer_input_cursor_for_screen_position(click_cursor_snapshot, 8, cursor_base),
         "tui composer hit-tests visible input rows to draft cursor byte offsets and ignores non-input rows");
  auto const multiline_click_snapshot = ava::tui::ComposerSnapshot{.mode = "build",
                                                                   .provider = "openai",
                                                                   .model = "gpt-5.5",
                                                                   .session_id = "session_test",
                                                                   .input = "one\ntwo\nthree",
                                                                   .status = "ready",
                                                                   .transcript = {},
                                                                   .width = 80,
                                                                   .height = 8};
  auto const clicked_second_line = ava::tui::composer_input_cursor_for_screen_position(multiline_click_snapshot, 6, cursor_base + 1);
  expect(clicked_second_line && *clicked_second_line == std::string("one\nt").size(),
         "tui composer hit-tests multiline visible input rows to the matching logical line cursor");
  auto const wide_click_text = std::string("a") + "\xE7\x95\x8C" + "b";
  auto const wide_click_cursor = ava::tui::composer_input_cursor_for_screen_position(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                                .provider = "openai",
                                                                                                                .model = "gpt-5.5",
                                                                                                                .session_id = "session_test",
                                                                                                                .input = wide_click_text,
                                                                                                                .status = "ready",
                                                                                                                .transcript = {},
                                                                                                                .width = 80,
                                                                                                                .height = 8},
                                                                                     7, cursor_base + 3);
  expect(wide_click_cursor && *wide_click_cursor == std::string("a").size() + std::string("\xE7\x95\x8C").size(),
         "tui composer click-to-cursor clamps through wide utf-8 cells without landing inside a codepoint");
  auto const selected_input_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                         .provider = "openai",
                                                                                         .model = "gpt-5.5",
                                                                                         .session_id = "session_test",
                                                                                         .input = "alpha beta",
                                                                                         .status = "ready",
                                                                                         .transcript = {},
                                                                                         .width = 80,
                                                                                         .height = 8,
                                                                                         .input_cursor = 10,
                                                                                         .input_selection_start = 6,
                                                                                         .input_selection_end = 10});
  expect(std::ranges::any_of(selected_input_frame,
                             [](std::string const& line) {
                               return line.find(std::string(ava::tui::detail::kReverseVideo) + "beta") != std::string::npos &&
                                      strip_sgr(line).find("│  alpha beta") != std::string::npos;
                             }),
         "tui composer renders selected input text with reverse video without changing visible draft text");

  auto const composer_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "plan",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "hello",
                                                                                   .status = "ready",
                                                                                   .transcript = {},
                                                                                   .width = 40,
                                                                                   .height = 8});
  expect(composer_frame.size() == 8, "tui composer frame fills the requested terminal height");
  expect(std::ranges::any_of(composer_frame, [](std::string const& line) { return strip_sgr(line).find("│  hello") != std::string::npos; }),
         "tui composer frame renders the input prompt content");
  auto const wide_frame = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = std::string("wide ") + "\xE6\xBC\xA2\xE6\xBC\xA2\xF0\x9F\x98\x80",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2"}},
                                 .width = 24,
                                 .height = 10});
  expect(std::ranges::all_of(wide_frame, [](std::string const& line) { return visible_columns(line) <= 24; }),
         "tui treats CJK and emoji as wide cells when fitting rendered lines");
  ava::tui::clear_terminal_signal();
  expect(!ava::tui::terminal_signal_received(), "tui terminal signal state can be cleared before curses entry");

  auto const permission_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "do not focus composer",
      .status = "permission required",
      .transcript = {},
      .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""},
      .width = 60,
      .height = 12});
  expect(permission_frame.size() == 12 &&
             std::ranges::any_of(permission_frame,
                                 [](std::string const& line) { return strip_sgr(line).find("│  do not focus composer") != std::string::npos; }) &&
             std::ranges::any_of(permission_frame, [](std::string const& line) { return strip_sgr(line).find("Permission required") != std::string::npos; }),
         "tui composer frame renders permission dock above composer while active");
  for (std::size_t height = 8; height <= 11; ++height)
  {
    auto compact_permission = ava::tui::ComposerSnapshot{};
    compact_permission.mode = "build";
    compact_permission.provider = "openai";
    compact_permission.model = "gpt-5.5";
    compact_permission.session_id = "session_test";
    compact_permission.input = "draft one\ndraft two\ndraft three\ndraft four\ndraft five\ndraft six";
    compact_permission.status = "permission required";
    compact_permission.permission_prompt =
        ava::tui::PermissionPromptView{.tool_name = "bash", .operation = "bash", .target = "", .command = "true", .reason = ""};
    compact_permission.width = 64;
    compact_permission.height = height;
    auto const frame = ava::tui::render_composer(compact_permission);
    expect(std::ranges::any_of(frame, [](std::string const& line) { return strip_sgr(line).find("Permission required") != std::string::npos; }) &&
               std::ranges::any_of(frame, [](std::string const& line) { return strip_sgr(line).find("[Allow once]") != std::string::npos; }),
           "minimum-height permission dock remains visible above a wrapped composer draft");
  }

  auto const question_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "do not focus composer",
      .status = "question required",
      .transcript = {},
      .question_prompt =
          ava::tui::QuestionPromptView{.header = "Choose tools",
                                       .question = "Pick the tools to run",
                                       .options = {ava::tui::QuestionPromptOptionView{.value = "read", .label = "Read files"},
                                                   ava::tui::QuestionPromptOptionView{.value = "grep", .label = "Search text", .selected = true}},
                                       .multiple = true,
                                       .allow_custom = true,
                                       .selected_option_index = 1,
                                       .custom_text = "explain"},
      .width = 64,
      .height = 12});
  expect(
      question_frame.size() == 12 &&
          std::ranges::any_of(question_frame, [](std::string const& line) { return strip_sgr(line).find("│  do not focus composer") != std::string::npos; }) &&
          std::ranges::any_of(question_frame,
                              [](std::string const& line) { return strip_sgr(line).find("Choose tools (multi-select)") != std::string::npos; }) &&
          std::ranges::any_of(question_frame, [](std::string const& line) { return strip_sgr(line).find("2. [x] Search text") != std::string::npos; }) &&
          std::ranges::any_of(question_frame, [](std::string const& line) { return strip_sgr(line).find("Custom: explain") != std::string::npos; }),
      "tui composer frame renders multi-select question dock above composer while active");

  std::vector<ava::tui::QuestionPromptOptionView> long_question_options;
  for (std::size_t index = 0; index < 12; ++index)
  {
    long_question_options.push_back(ava::tui::QuestionPromptOptionView{.value = "option-" + std::to_string(index), .label = "Option " + std::to_string(index)});
  }
  auto question_navigation_prompt = ava::tui::QuestionPromptView{
      .header = "Choose one", .question = "Pick an option", .options = long_question_options, .selected_option_index = 0, .custom_text = {}};
  auto question_navigation = ava::tui::handle_question_prompt_input(question_navigation_prompt, ava::tui::InputEvent{.key = ava::tui::Key::ArrowDown});
  expect(question_navigation.action == ava::tui::QuestionPromptInputAction::Redraw && question_navigation.selected_option_index == 1,
         "question modal down arrow advances the selected option");
  question_navigation_prompt.selected_option_index = question_navigation.selected_option_index;
  question_navigation = ava::tui::handle_question_prompt_input(question_navigation_prompt, ava::tui::InputEvent{.key = ava::tui::Key::PageDown});
  expect(question_navigation.action == ava::tui::QuestionPromptInputAction::Redraw && question_navigation.selected_option_index == 6,
         "question modal PageDown advances by a viewport-sized page");
  question_navigation_prompt.selected_option_index = question_navigation.selected_option_index;
  question_navigation = ava::tui::handle_question_prompt_input(question_navigation_prompt, ava::tui::InputEvent{.key = ava::tui::Key::End});
  expect(question_navigation.action == ava::tui::QuestionPromptInputAction::Redraw && question_navigation.selected_option_index == 11,
         "question modal End moves to the final option");
  question_navigation_prompt.selected_option_index = question_navigation.selected_option_index;
  question_navigation = ava::tui::handle_question_prompt_input(question_navigation_prompt, ava::tui::InputEvent{.key = ava::tui::Key::MouseWheelUp});
  expect(question_navigation.action == ava::tui::QuestionPromptInputAction::Redraw && question_navigation.selected_option_index == 10,
         "question modal mouse wheel uses the same selection path as arrow navigation");

  auto const long_question_dock = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "question required",
      .transcript = {},
      .question_prompt =
          ava::tui::QuestionPromptView{
              .header = "Choose one", .question = "Pick an option", .options = long_question_options, .selected_option_index = 9, .custom_text = {}},
      .width = 64,
      .height = 12});
  expect(std::ranges::any_of(long_question_dock, [](std::string const& line) { return strip_sgr(line).find("10. Option 9") != std::string::npos; }),
         "question dock scrolls its option window to keep the selected row visible");
  bool every_question_dock_selection_visible = true;
  for (std::size_t index = 0; index < long_question_options.size(); ++index)
  {
    auto dock_snapshot = ava::tui::ComposerSnapshot{};
    dock_snapshot.mode = "build";
    dock_snapshot.provider = "openai";
    dock_snapshot.model = "gpt-5.5";
    dock_snapshot.session_id = "session_test";
    dock_snapshot.question_prompt = question_navigation_prompt;
    dock_snapshot.question_prompt->selected_option_index = index;
    dock_snapshot.width = 64;
    dock_snapshot.height = 12;
    auto const frame = ava::tui::render_composer(dock_snapshot);
    auto const wanted = std::to_string(index + 1) + ". Option " + std::to_string(index);
    every_question_dock_selection_visible &=
        std::ranges::any_of(frame, [&](std::string const& line) { return strip_sgr(line).find(wanted) != std::string::npos; });
  }
  expect(every_question_dock_selection_visible, "question dock viewport follows every selected option beyond the initial screen");
  for (std::size_t height = 8; height <= 11; ++height)
  {
    auto compact_dock = ava::tui::ComposerSnapshot{};
    compact_dock.mode = "build";
    compact_dock.provider = "openai";
    compact_dock.model = "gpt-5.5";
    compact_dock.session_id = "session_test";
    compact_dock.input = "draft one\ndraft two\ndraft three\ndraft four\ndraft five\ndraft six";
    compact_dock.status = "question required";
    compact_dock.question_prompt = question_navigation_prompt;
    compact_dock.question_prompt->selected_option_index = 9;
    compact_dock.width = 64;
    compact_dock.height = height;
    auto const frame = ava::tui::render_composer(compact_dock);
    expect(std::ranges::any_of(frame, [](std::string const& line) { return strip_sgr(line).find("10. Option 9") != std::string::npos; }),
           "minimum-height question dock keeps the selected option visible above a wrapped composer draft");
  }

  auto const secret_question_frame =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "question required",
                                                           .transcript = {},
                                                           .question_prompt = ava::tui::QuestionPromptView{.header = "Connect",
                                                                                                           .question = "Paste API key",
                                                                                                           .options = {},
                                                                                                           .multiple = false,
                                                                                                           .allow_custom = true,
                                                                                                           .secret = true,
                                                                                                           .custom_text = "sk-visible-secret"},
                                                           .width = 64,
                                                           .height = 9});
  expect(std::ranges::none_of(secret_question_frame, [](std::string const& line) { return strip_sgr(line).find("sk-visible-secret") != std::string::npos; }) &&
             std::ranges::any_of(secret_question_frame,
                                 [](std::string const& line) { return strip_sgr(line).find("Custom: *****************") != std::string::npos; }),
         "tui question dock masks secret custom input");

  auto const modal_question_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "composer stays behind modal",
      .status = "question required",
      .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "background transcript"}},
      .question_prompt = ava::tui::QuestionPromptView{.header = "Connect a provider",
                                                      .question = "Select provider",
                                                      .options = {ava::tui::QuestionPromptOptionView{.value = "openai", .label = "OpenAI"},
                                                                  ava::tui::QuestionPromptOptionView{.value = "anthropic", .label = "Anthropic"}},
                                                      .multiple = false,
                                                      .allow_custom = true,
                                                      .secret = false,
                                                      .modal = true,
                                                      .searchable = true,
                                                      .selected_option_index = 1,
                                                      .custom_text = "anth"},
      .width = 80,
      .height = 16});
  expect(
      modal_question_frame.size() == 16 &&
          std::ranges::any_of(modal_question_frame, [](std::string const& line) { return strip_sgr(line).find("Connect a provider") != std::string::npos; }) &&
          std::ranges::any_of(modal_question_frame, [](std::string const& line) { return strip_sgr(line).find("Search: anth") != std::string::npos; }) &&
          std::ranges::any_of(modal_question_frame, [](std::string const& line) { return strip_sgr(line).find("Anthropic") != std::string::npos; }) &&
          std::ranges::none_of(modal_question_frame, [](std::string const& line) { return strip_sgr(line).find("OpenAI") != std::string::npos; }),
      "tui renders searchable provider questions as centered filtered modals");

  auto long_modal_prompt = ava::tui::QuestionPromptView{.header = "Choose a provider",
                                                        .question = "Select provider",
                                                        .options = long_question_options,
                                                        .modal = true,
                                                        .searchable = true,
                                                        .selected_option_index = 9,
                                                        .custom_text = {}};
  auto const long_modal_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "",
                                                                                     .status = "question required",
                                                                                     .transcript = {},
                                                                                     .question_prompt = long_modal_prompt,
                                                                                     .width = 64,
                                                                                     .height = 12});
  expect(std::ranges::any_of(long_modal_frame, [](std::string const& line) { return strip_sgr(line).find("Option 9") != std::string::npos; }),
         "question modal scrolls its option window to keep the selected row visible");
  bool every_question_modal_selection_visible = true;
  for (std::size_t index = 0; index < long_question_options.size(); ++index)
  {
    auto modal_snapshot = ava::tui::ComposerSnapshot{};
    modal_snapshot.mode = "build";
    modal_snapshot.provider = "openai";
    modal_snapshot.model = "gpt-5.5";
    modal_snapshot.session_id = "session_test";
    modal_snapshot.question_prompt = long_modal_prompt;
    modal_snapshot.question_prompt->selected_option_index = index;
    modal_snapshot.width = 64;
    modal_snapshot.height = 12;
    auto const frame = ava::tui::render_composer(modal_snapshot);
    auto const wanted = "Option " + std::to_string(index);
    every_question_modal_selection_visible &=
        std::ranges::any_of(frame, [&](std::string const& line) { return strip_sgr(line).find(wanted) != std::string::npos; });
  }
  expect(every_question_modal_selection_visible, "question modal viewport follows every selected option beyond the initial screen");
  for (std::size_t height = 8; height <= 12; ++height)
  {
    for (std::size_t selected = 0; selected < long_question_options.size(); ++selected)
    {
      auto compact_modal = ava::tui::ComposerSnapshot{};
      compact_modal.mode = "build";
      compact_modal.provider = "openai";
      compact_modal.model = "gpt-5.5";
      compact_modal.session_id = "session_test";
      compact_modal.question_prompt = long_modal_prompt;
      compact_modal.question_prompt->selected_option_index = selected;
      compact_modal.width = 64;
      compact_modal.height = height;
      auto const frame = ava::tui::render_composer(compact_modal);
      auto const selected_label = "Option " + std::to_string(selected);
      expect(std::ranges::any_of(frame,
                                 [&](std::string const& line) {
                                   auto const text = strip_sgr(line);
                                   return text.find("›") != std::string::npos && text.find(selected_label) != std::string::npos;
                                 }),
             "integrated question modal keeps every selected option visible at compact terminal heights");
    }
  }

  auto const oauth_modal_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "question required",
      .transcript = {},
      .question_prompt = ava::tui::QuestionPromptView{.header = "ChatGPT Pro/Plus (browser)",
                                                      .question = "https://auth.openai.com/oauth/authorize?client_id="
                                                                  "app_EMoamEEZ73f0CkXaXp7hrann&redirect_uri=http%3A%2F%2Flocalhost%3A1455"
                                                                  "%2Fauth%2Fcallback&code_challenge=longchallengevalue&state=state\n"
                                                                  "\nComplete authorization in your browser. This window will close automatically."
                                                                  "\n\nWaiting for authorization...",
                                                      .options = {ava::tui::QuestionPromptOptionView{.value = "copy:https://auth.openai.com/oauth/authorize",
                                                                                                     .label = "C Copy"}},
                                                      .multiple = false,
                                                      .allow_custom = false,
                                                      .secret = false,
                                                      .modal = true,
                                                      .searchable = false,
                                                      .selected_option_index = 0,
                                                      .custom_text = ""},
      .width = 80,
      .height = 20});
  expect(std::ranges::any_of(oauth_modal_frame, [](std::string const& line) { return strip_sgr(line).find("https://auth.openai.com") != std::string::npos; }) &&
             std::ranges::any_of(oauth_modal_frame, [](std::string const& line) { return strip_sgr(line).find("C Copy") != std::string::npos; }) &&
             std::ranges::any_of(oauth_modal_frame,
                                 [](std::string const& line) { return strip_sgr(line).find("Waiting for authorization") != std::string::npos; }) &&
             std::ranges::any_of(oauth_modal_frame, [](std::string const& line) { return strip_sgr(line).find("C copy") != std::string::npos; }) &&
             std::ranges::none_of(oauth_modal_frame, [](std::string const& line) { return strip_sgr(line).find("Enter confirm") != std::string::npos; }),
         "tui renders OpenAI OAuth modal with opencode-style waiting and C copy shortcut");

  auto const sidebar_modal_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "question required",
      .transcript = {},
      .question_prompt = ava::tui::QuestionPromptView{.header = "Connect OpenAI",
                                                      .question = "Choose login method",
                                                      .options = {ava::tui::QuestionPromptOptionView{.value = "api_key", .label = "A OpenAI API key"}},
                                                      .multiple = false,
                                                      .allow_custom = false,
                                                      .secret = false,
                                                      .modal = true,
                                                      .searchable = false,
                                                      .selected_option_index = 0,
                                                      .custom_text = ""},
      .width = 128,
      .height = 22,
      .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                           .mode = "build",
                                           .provider = "openai",
                                           .model = "gpt-5.5",
                                           .workspace = "/workspace",
                                           .git_branch = "develop",
                                           .version = "0.32",
                                           .context_source_count = 1}});
  expect(std::ranges::any_of(sidebar_modal_frame,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("Connect OpenAI") != std::string::npos && visible.find("│") != std::string::npos;
                             }) &&
             std::ranges::any_of(sidebar_modal_frame, [](std::string const& line) { return strip_sgr(line).find("Modified Files") != std::string::npos; }) &&
             std::ranges::any_of(sidebar_modal_frame, [](std::string const& line) { return strip_sgr(line).find("Session") != std::string::npos; }),
         "tui modal overlays the main pane without hiding the sidebar");

  auto searchable_question = ava::tui::QuestionPromptView{.header = "Connect a provider",
                                                          .question = "Select provider",
                                                          .options = {ava::tui::QuestionPromptOptionView{.value = "openai", .label = "OpenAI"},
                                                                      ava::tui::QuestionPromptOptionView{.value = "anthropic", .label = "Anthropic"}},
                                                          .multiple = false,
                                                          .allow_custom = true,
                                                          .secret = false,
                                                          .modal = true,
                                                          .searchable = true,
                                                          .selected_option_index = 0,
                                                          .custom_text = ""};
  auto searchable_input = ava::tui::handle_question_prompt_input(searchable_question, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'h'});
  expect(searchable_input.action == ava::tui::QuestionPromptInputAction::Redraw && searchable_input.custom_text == "h" &&
             searchable_input.selected_option_index == 1,
         "searchable question typing filters and moves selection to the first match");
  searchable_question.custom_text = "anth";
  searchable_question.selected_option_index = 1;
  searchable_input = ava::tui::handle_question_prompt_input(searchable_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(searchable_input.action == ava::tui::QuestionPromptInputAction::Resolve && searchable_input.options[1].selected,
         "searchable question enter selects the matched provider option");
  searchable_question.custom_text = "custom-provider";
  searchable_question.selected_option_index = 0;
  searchable_input = ava::tui::handle_question_prompt_input(searchable_question, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  auto const custom_search_answer =
      ava::tui::question_answer_from_prompt_view(ava::tui::QuestionPromptView{.header = searchable_question.header,
                                                                              .question = searchable_question.question,
                                                                              .options = searchable_input.options,
                                                                              .multiple = searchable_question.multiple,
                                                                              .allow_custom = searchable_question.allow_custom,
                                                                              .secret = searchable_question.secret,
                                                                              .modal = searchable_question.modal,
                                                                              .searchable = searchable_question.searchable,
                                                                              .selected_option_index = searchable_input.selected_option_index,
                                                                              .custom_text = searchable_input.custom_text});
  expect(custom_search_answer && custom_search_answer->selected_options.empty() && custom_search_answer->custom_text == "custom-provider",
         "searchable question enter resolves custom provider ids when no option matches");

  auto selector = ava::tui::SelectListView{.title = "Pick model",
                                           .subtitle = std::string("Choose a provider/model pair ") + "\x1b[31m" + "unsafe",
                                           .items = {ava::tui::SelectListItemView{.value = "openai/gpt-5.5",
                                                                                  .label = "GPT-5.5",
                                                                                  .description = "openai/gpt-5.5",
                                                                                  .group = "openai",
                                                                                  .detail = "tools yes · reasoning yes",
                                                                                  .badge = "reasoning",
                                                                                  .current = true,
                                                                                  .enabled = true,
                                                                                  .disabled_reason = {}},
                                                     ava::tui::SelectListItemView{.value = "anthropic/claude-sonnet-4-5",
                                                                                  .label = "Claude Sonnet 4.5",
                                                                                  .description = "anthropic/claude-sonnet-4-5",
                                                                                  .group = "anthropic",
                                                                                  .detail = "tools yes · reasoning no",
                                                                                  .badge = {},
                                                                                  .current = false,
                                                                                  .enabled = true,
                                                                                  .disabled_reason = {}},
                                                     ava::tui::SelectListItemView{.value = "ghost/model",
                                                                                  .label = "Ghost Model",
                                                                                  .description = "ghost/model",
                                                                                  .group = "ghost",
                                                                                  .detail = "tools ?",
                                                                                  .badge = {},
                                                                                  .current = false,
                                                                                  .enabled = false,
                                                                                  .disabled_reason = "provider unavailable"}},
                                           .selected_item_index = 0,
                                           .query = {},
                                           .placeholder = "Search models",
                                           .empty_text = "No matches",
                                           .footer_hint = "Enter choose · Esc cancel"};
  auto scrolling_selector = ava::tui::SelectListView{.title = "Pick model",
                                                     .subtitle = "Current openai/gpt-5.6-sol · selection is validated by the backend before session mutation",
                                                     .items = {},
                                                     .selected_item_index = 0,
                                                     .query = {},
                                                     .placeholder = "Search models",
                                                     .empty_text = "No matches",
                                                     .footer_hint = {}};
  for (std::size_t index = 0; index < 14; ++index)
  {
    scrolling_selector.items.push_back(ava::tui::SelectListItemView{.value = "model-" + std::to_string(index),
                                                                    .label = "Model " + std::to_string(index),
                                                                    .description = {},
                                                                    .group = "provider-" + std::to_string(index / 3),
                                                                    .detail = {},
                                                                    .badge = {},
                                                                    .current = false,
                                                                    .enabled = true,
                                                                    .disabled_reason = {}});
  }
  scrolling_selector.selected_item_index = 10;
  auto const scrolling_selector_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                             .provider = "openai",
                                                                                             .model = "gpt-5.5",
                                                                                             .session_id = "session_test",
                                                                                             .input = "",
                                                                                             .status = "selecting",
                                                                                             .transcript = {},
                                                                                             .select_list = scrolling_selector,
                                                                                             .width = 64,
                                                                                             .height = 16});
  expect(std::ranges::any_of(scrolling_selector_frame, [](std::string const& line) { return strip_sgr(line).find("Model 10") != std::string::npos; }),
         "grouped select-list modals scroll far enough to keep the selected row visible");
  auto const scrolling_selected_row =
      std::ranges::find_if(scrolling_selector_frame, [](std::string const& line) { return strip_sgr(line).find("Model 10") != std::string::npos; });
  auto scrolling_hit_snapshot = ava::tui::ComposerSnapshot{};
  scrolling_hit_snapshot.mode = "build";
  scrolling_hit_snapshot.provider = "openai";
  scrolling_hit_snapshot.model = "gpt-5.5";
  scrolling_hit_snapshot.session_id = "session_test";
  scrolling_hit_snapshot.select_list = scrolling_selector;
  scrolling_hit_snapshot.width = 64;
  scrolling_hit_snapshot.height = 16;
  auto const scrolling_hit = scrolling_selected_row == scrolling_selector_frame.end()
                                 ? std::optional<std::size_t>{}
                                 : ava::tui::select_list_selection_for_screen_position(
                                       scrolling_hit_snapshot, static_cast<std::size_t>(scrolling_selected_row - scrolling_selector_frame.begin()) + 1, 8);
  expect(scrolling_hit && *scrolling_hit == 10, "grouped select-list modal hit testing shares the rendered scrolling window");
  bool every_select_list_selection_visible = true;
  for (std::size_t index = 0; index < scrolling_selector.items.size(); ++index)
  {
    auto selected_view = scrolling_selector;
    selected_view.selected_item_index = index;
    auto selected_snapshot = ava::tui::ComposerSnapshot{};
    selected_snapshot.mode = "build";
    selected_snapshot.provider = "openai";
    selected_snapshot.model = "gpt-5.5";
    selected_snapshot.session_id = "session_test";
    selected_snapshot.select_list = std::move(selected_view);
    selected_snapshot.width = 64;
    selected_snapshot.height = 16;
    auto const frame = ava::tui::render_composer(selected_snapshot);
    auto const wanted = "Model " + std::to_string(index);
    every_select_list_selection_visible &=
        std::ranges::any_of(frame, [&](std::string const& line) { return strip_sgr(line).find(wanted) != std::string::npos; });
  }
  expect(every_select_list_selection_visible, "grouped select-list viewport follows every selected item beyond the initial screen");
  for (std::size_t height = 8; height <= 12; ++height)
  {
    for (std::size_t selected = 0; selected < scrolling_selector.items.size(); ++selected)
    {
      scrolling_selector.selected_item_index = selected;
      auto compact_snapshot = ava::tui::ComposerSnapshot{};
      compact_snapshot.mode = "build";
      compact_snapshot.provider = "openai";
      compact_snapshot.model = "gpt-5.5";
      compact_snapshot.session_id = "session_test";
      compact_snapshot.select_list = scrolling_selector;
      compact_snapshot.width = 64;
      compact_snapshot.height = height;
      auto const frame = ava::tui::render_composer(compact_snapshot);
      auto const selected_label = "Model " + std::to_string(selected);
      auto const selected_line = std::ranges::find_if(frame, [&](std::string const& line) {
        auto const text = strip_sgr(line);
        return text.find("›") != std::string::npos && text.find(selected_label) != std::string::npos;
      });
      expect(selected_line != frame.end(), "integrated select-list modal keeps every selected item visible at compact terminal heights");
      if (selected_line != frame.end())
      {
        auto const hit = ava::tui::select_list_selection_for_screen_position(compact_snapshot, static_cast<std::size_t>(selected_line - frame.begin()) + 1, 8);
        expect(hit && *hit == selected, "integrated select-list modal hit testing matches every compact rendered selection");
      }
    }
  }
  for (std::size_t selected = 0; selected < scrolling_selector.items.size(); ++selected)
  {
    scrolling_selector.selected_item_index = selected;
    auto const compact_lines = ava::tui::detail::render_select_list_modal(scrolling_selector, 64, 8);
    auto const selected_label = "Model " + std::to_string(selected);
    auto const selected_line = std::ranges::find_if(compact_lines, [&](std::string const& line) {
      auto const text = strip_sgr(line);
      return text.find("›") != std::string::npos && text.find(selected_label) != std::string::npos;
    });
    expect(selected_line != compact_lines.end(), "minimum-height select-list modal reserves a visible row for every selection despite a wrapped subtitle");
    if (selected_line != compact_lines.end())
    {
      auto const hit =
          ava::tui::detail::select_list_item_for_modal_row(scrolling_selector, static_cast<std::size_t>(selected_line - compact_lines.begin()), 64, 8);
      expect(hit && *hit == selected, "minimum-height select-list modal hit testing matches every rendered selection");
    }
  }

  auto boundary_selector = ava::tui::SelectListView{
      .title = "Group boundary",
      .subtitle = {},
      .items = {ava::tui::SelectListItemView{
                    .value = "a-0", .label = "A zero", .description = {}, .group = "provider-a", .detail = {}, .badge = {}, .disabled_reason = {}},
                ava::tui::SelectListItemView{
                    .value = "a-1", .label = "A one", .description = {}, .group = "provider-a", .detail = {}, .badge = {}, .disabled_reason = {}},
                ava::tui::SelectListItemView{
                    .value = "b-0", .label = "B zero", .description = {}, .group = "provider-b", .detail = {}, .badge = {}, .disabled_reason = {}}},
      .selected_item_index = 2,
      .query = {},
      .placeholder = "Search",
      .empty_text = "No matches",
      .footer_hint = {}};
  auto const boundary_lines = ava::tui::detail::render_select_list_modal(boundary_selector, 64, 10);
  auto const boundary_selected = std::ranges::find_if(boundary_lines, [](std::string const& line) {
    auto const text = strip_sgr(line);
    return text.find("›") != std::string::npos && text.find("B zero") != std::string::npos;
  });
  expect(boundary_selected != boundary_lines.end() && boundary_selected != boundary_lines.begin() &&
             strip_sgr(*(boundary_selected - 1)).find("provider-b") != std::string::npos,
         "grouped select-list modal never renders the first item of a new group beneath the previous group's header");
  if (boundary_selected != boundary_lines.end())
  {
    auto const hit =
        ava::tui::detail::select_list_item_for_modal_row(boundary_selector, static_cast<std::size_t>(boundary_selected - boundary_lines.begin()), 64, 10);
    expect(hit && *hit == 2, "group-boundary select-list hit testing matches the rendered selected item");
  }

  selector.query = "sonnet";
  auto selector_matches = ava::tui::filter_select_list_items(selector);
  expect(selector_matches.size() == 1 && selector_matches.front() == 1,
         "select-list fuzzy filter matches provider/model labels and preserves original row ids");
  auto selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'g'});
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.query == "sonnetg" && selector_input.selected_item_index == 0,
         "select-list typing updates the search query and clamps to empty-match selection safely");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::Space});
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.query == "sonnet ",
         "select-list semantic Space remains searchable text instead of losing spaces in modal queries");
  auto const selector_keybinds = ava::tui::parse_key_bindings_json(
      "{\"tui.select.confirm\":[\"Enter\",\"Space\"],\"tui.select.cancel\":[\"Escape\",\"Ctrl+W\"],"
      "\"tui.select.up\":\"Ctrl+P\",\"tui.select.down\":\"Ctrl+N\","
      "\"tui.select.pageUp\":\"Ctrl+O\",\"tui.select.pageDown\":\"Ctrl+Y\"}");
  expect(static_cast<bool>(selector_keybinds), "select-list custom keybind fixture parses");
  auto const selector_bindings = selector_keybinds ? *selector_keybinds : ava::tui::default_key_bindings();
  auto bound_selector = selector;
  bound_selector.query.clear();
  bound_selector.selected_item_index = 0;
  selector_input = ava::tui::handle_select_list_input(bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlN}, selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 1,
         "select-list uses configured down binding before raw Ctrl+N modal actions");
  bound_selector.selected_item_index = 1;
  selector_input = ava::tui::handle_select_list_input(bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlP}, selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 0,
         "select-list uses configured up binding before raw Ctrl+P modal actions");
  bound_selector.selected_item_index = 0;
  selector_input = ava::tui::handle_select_list_input(bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::Space}, selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Resolve && selector_input.selected_item_index == 0,
         "select-list configured Space confirms instead of appending query text");
  selector_input = ava::tui::handle_select_list_input(bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlW}, selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Cancel,
         "select-list uses configured cancel binding before raw composer delete-word actions");
  auto const session_selector_keybinds = ava::tui::parse_key_bindings_json(
      "{\"app.session.togglePath\":\"Ctrl+O\",\"app.session.toggleSort\":\"Ctrl+Y\","
      "\"app.session.toggleNamedFilter\":\"Ctrl+U\",\"app.session.rename\":\"Ctrl+K\","
      "\"app.session.delete\":\"Alt+D\"}");
  expect(static_cast<bool>(session_selector_keybinds), "session selector custom keybind fixture parses");
  auto const session_selector_bindings = session_selector_keybinds ? *session_selector_keybinds : ava::tui::default_key_bindings();
  auto session_bound_selector = selector;
  session_bound_selector.query = "custom";
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlO}, session_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::TogglePathDisplay && selector_input.query == session_bound_selector.query,
         "select-list routes custom app.session.togglePath binding before raw composer details actions");
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlY}, session_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::CycleSort && selector_input.query == session_bound_selector.query,
         "select-list routes custom app.session.toggleSort binding before raw composer yank actions");
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlU}, session_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::ToggleNamedFilter && selector_input.query == session_bound_selector.query,
         "select-list routes custom app.session.toggleNamedFilter binding before raw composer line deletion");
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlK}, session_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Rename && selector_input.query == session_bound_selector.query,
         "select-list routes custom app.session.rename binding before raw composer line-end deletion");
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::AltD}, session_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Archive && selector_input.query == session_bound_selector.query,
         "select-list routes custom app.session.delete binding before raw composer word deletion");
  auto const tree_selector_keybinds = ava::tui::parse_key_bindings_json(
      "{\"app.tree.foldOrUp\":\"Ctrl+O\","
      "\"app.tree.unfoldOrDown\":\"Ctrl+Y\","
      "\"app.tree.editLabel\":\"Shift+L\","
      "\"app.tree.toggleLabelTimestamp\":\"Shift+T\","
      "\"app.tree.filter.labeledOnly\":\"Ctrl+Space\","
      "\"app.tree.filter.all\":\"Ctrl+/\"}");
  expect(static_cast<bool>(tree_selector_keybinds), "tree selector custom keybind fixture parses");
  auto const tree_selector_bindings = tree_selector_keybinds ? *tree_selector_keybinds : ava::tui::default_key_bindings();
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlO}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchParent && selector_input.query == session_bound_selector.query,
         "select-list routes custom app.tree.foldOrUp binding before raw composer details actions");
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlY}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchChild && selector_input.query == session_bound_selector.query,
         "select-list routes custom app.tree.unfoldOrDown binding before raw composer yank actions");
  selector_input = ava::tui::handle_select_list_input(
      session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'L', .text = "L"}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Label && selector_input.query == session_bound_selector.query,
         "select-list routes Pi app.tree.editLabel Shift+L binding to the existing label draft action");
  selector_input = ava::tui::handle_select_list_input(
      session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'l', .text = "l"}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.query == session_bound_selector.query + "l",
         "select-list keeps ordinary lowercase l as searchable text when Shift+L is bound");
  selector_input = ava::tui::handle_select_list_input(
      session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 'T', .text = "T"}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::ToggleLabelTimestamp && selector_input.query == session_bound_selector.query,
         "select-list routes Pi app.tree.toggleLabelTimestamp Shift+T binding to the label-time toggle");
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlSpace}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::ToggleNamedFilter && selector_input.query == session_bound_selector.query,
         "select-list routes Pi app.tree.filter.labeledOnly to the existing named/labeled filter");
  selector_input = ava::tui::handle_select_list_input(session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlSlash}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::ToggleArchivedFilter && selector_input.query == session_bound_selector.query,
         "select-list routes Pi app.tree.filter.all to the existing archived visibility filter");
  selector_input = ava::tui::handle_select_list_input(
      session_bound_selector, ava::tui::InputEvent{.key = ava::tui::Key::Character, .character = 't', .text = "t"}, tree_selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.query == session_bound_selector.query + "t",
         "select-list keeps ordinary lowercase t as searchable text when Shift+T is bound");
  selector.query = "ghost";
  selector.selected_item_index = 2;
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw, "select-list enter refuses disabled model/provider rows without resolving");
  selector.query = "claude";
  selector.selected_item_index = 0;
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::Enter});
  expect(selector_input.action == ava::tui::SelectListInputAction::Resolve && selector_input.selected_item_index == 1,
         "select-list enter resolves the highlighted enabled fuzzy match");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::Escape});
  expect(selector_input.action == ava::tui::SelectListInputAction::Cancel, "select-list escape cancels the modal safely");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::MouseLeftClick});
  expect(selector_input.action == ava::tui::SelectListInputAction::None && selector_input.query == selector.query,
         "select-list raw mouse clicks are resolved by rendered modal hit-testing instead of triggering keyboard-only actions");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlT});
  expect(selector_input.action == ava::tui::SelectListInputAction::CycleSort && selector_input.query == selector.query,
         "select-list ctrl+t exposes a modal sort-cycle action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlS});
  expect(selector_input.action == ava::tui::SelectListInputAction::CycleSort && selector_input.query == selector.query,
         "select-list ctrl+s exposes the Pi-compatible modal sort-cycle action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlN});
  expect(selector_input.action == ava::tui::SelectListInputAction::ToggleNamedFilter && selector_input.query == selector.query,
         "select-list ctrl+n exposes a modal named-session filter action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlP});
  expect(selector_input.action == ava::tui::SelectListInputAction::TogglePathDisplay && selector_input.query == selector.query,
         "select-list ctrl+p exposes a modal path-display toggle without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlA});
  expect(selector_input.action == ava::tui::SelectListInputAction::ToggleArchivedFilter && selector_input.query == selector.query,
         "select-list ctrl+a exposes a modal archived-session filter toggle without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlX});
  expect(selector_input.action == ava::tui::SelectListInputAction::ModelsClearAll && selector_input.query == selector.query,
         "select-list ctrl+x exposes a scoped-model clear action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlR});
  expect(selector_input.action == ava::tui::SelectListInputAction::Rename && selector_input.query == selector.query,
         "select-list ctrl+r exposes a modal rename action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlL});
  expect(selector_input.action == ava::tui::SelectListInputAction::Label && selector_input.query == selector.query,
         "select-list ctrl+l exposes a modal label action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlD});
  expect(selector_input.action == ava::tui::SelectListInputAction::Archive && selector_input.query == selector.query,
         "select-list ctrl+d exposes a modal archive action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlBackspace});
  expect(selector_input.action == ava::tui::SelectListInputAction::None && selector_input.query == selector.query,
         "select-list ctrl+backspace is non-destructive while the search query is non-empty");
  auto empty_query_selector = selector;
  empty_query_selector.query.clear();
  selector_input = ava::tui::handle_select_list_input(empty_query_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlBackspace});
  expect(selector_input.action == ava::tui::SelectListInputAction::ArchiveNoninvasive && selector_input.query.empty(),
         "select-list ctrl+backspace archives only when the selector search query is empty");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::AltArrowLeft});
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchParent && selector_input.query == selector.query,
         "select-list alt-left exposes a modal parent-branch action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlArrowLeft});
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchParent && selector_input.query == selector.query,
         "select-list ctrl-left exposes a modal parent-branch action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::AltArrowRight});
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchChild && selector_input.query == selector.query,
         "select-list alt-right exposes a modal child-branch action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlArrowRight});
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchChild && selector_input.query == selector.query,
         "select-list ctrl-right exposes a modal child-branch action without clearing search state");
  ava::tui::SelectListView paged_selector{.title = "Paged",
                                          .subtitle = {},
                                          .items = {},
                                          .selected_item_index = 0,
                                          .query = {},
                                          .placeholder = "Search rows",
                                          .empty_text = "No rows",
                                          .footer_hint = {}};
  for (int index = 0; index < 8; ++index)
  {
    paged_selector.items.push_back(ava::tui::SelectListItemView{.value = "row_" + std::to_string(index),
                                                                .label = "Row " + std::to_string(index),
                                                                .description = "Page navigation row",
                                                                .group = "Rows",
                                                                .detail = {},
                                                                .badge = {},
                                                                .current = false,
                                                                .enabled = true,
                                                                .disabled_reason = {}});
  }
  selector_input = ava::tui::handle_select_list_input(paged_selector, ava::tui::InputEvent{.key = ava::tui::Key::PageDown});
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 5,
         "select-list PageDown jumps five visible rows");
  auto bound_paged_selector = paged_selector;
  selector_input = ava::tui::handle_select_list_input(bound_paged_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlY}, selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 5,
         "select-list uses configured page-down binding before raw Ctrl+Y composer yank actions");
  bound_paged_selector.selected_item_index = 5;
  selector_input = ava::tui::handle_select_list_input(bound_paged_selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlO}, selector_bindings);
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 0,
         "select-list uses configured page-up binding before raw Ctrl+O details actions");
  paged_selector.selected_item_index = 5;
  selector_input = ava::tui::handle_select_list_input(paged_selector, ava::tui::InputEvent{.key = ava::tui::Key::PageDown});
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 7,
         "select-list PageDown clamps at the last matching row instead of wrapping");
  paged_selector.selected_item_index = 7;
  selector_input = ava::tui::handle_select_list_input(paged_selector, ava::tui::InputEvent{.key = ava::tui::Key::PageUp});
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 2,
         "select-list PageUp jumps five visible rows");
  paged_selector.selected_item_index = 0;
  selector_input = ava::tui::handle_select_list_input(paged_selector, ava::tui::InputEvent{.key = ava::tui::Key::PageUp});
  expect(selector_input.action == ava::tui::SelectListInputAction::Redraw && selector_input.selected_item_index == 0,
         "select-list PageUp clamps at the first matching row instead of wrapping");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::CtrlEnter});
  expect(selector_input.action == ava::tui::SelectListInputAction::None && selector_input.query == selector.query,
         "select-list ctrl+enter remains a composer-only newline alias and does not trigger modal actions");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::AltEnter});
  expect(selector_input.action == ava::tui::SelectListInputAction::None && selector_input.query == selector.query,
         "select-list alt+enter remains a composer-only follow-up/submit action and does not trigger modal actions");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::ShiftTab});
  expect(selector_input.action == ava::tui::SelectListInputAction::None && selector_input.query == selector.query,
         "select-list shift+tab remains an app-level reasoning shortcut and does not trigger modal actions");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::AltArrowLeft});
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchParent && selector_input.query == selector.query,
         "select-list alt+left triggers parent-branch navigation in modal context");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::AltArrowRight});
  expect(selector_input.action == ava::tui::SelectListInputAction::BranchChild && selector_input.query == selector.query,
         "select-list alt+right triggers child-branch navigation in modal context");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::AltArrowUp});
  expect(selector_input.action == ava::tui::SelectListInputAction::ModelsReorderUp && selector_input.query == selector.query,
         "select-list alt+up exposes a scoped-model reorder-up action without clearing search state");
  selector_input = ava::tui::handle_select_list_input(selector, ava::tui::InputEvent{.key = ava::tui::Key::AltArrowDown});
  expect(selector_input.action == ava::tui::SelectListInputAction::ModelsReorderDown && selector_input.query == selector.query,
         "select-list alt+down exposes a scoped-model reorder-down action without clearing search state");

  auto const selector_frame =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "composer behind selector",
                                                           .status = "selecting model",
                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "background transcript"}},
                                                           .select_list = selector,
                                                           .width = 88,
                                                           .height = 18});
  auto click_selector = selector;
  click_selector.query.clear();
  click_selector.selected_item_index = 0;
  auto const clicked_selector = ava::tui::select_list_selection_for_screen_position(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                               .provider = "openai",
                                                                                                               .model = "gpt-5.5",
                                                                                                               .session_id = "session_test",
                                                                                                               .input = "composer behind selector",
                                                                                                               .status = "selector opened",
                                                                                                               .transcript = {},
                                                                                                               .select_list = click_selector,
                                                                                                               .width = 92,
                                                                                                               .height = 18},
                                                                                    12, 12);
  expect(clicked_selector && *clicked_selector == 1, "select-list hit-test maps a visible modal item row back to its original item index");
  auto const outside_selector = ava::tui::select_list_selection_for_screen_position(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                                               .provider = "openai",
                                                                                                               .model = "gpt-5.5",
                                                                                                               .session_id = "session_test",
                                                                                                               .input = "composer behind selector",
                                                                                                               .status = "selector opened",
                                                                                                               .transcript = {},
                                                                                                               .select_list = click_selector,
                                                                                                               .width = 92,
                                                                                                               .height = 18},
                                                                                    12, 1);
  expect(!outside_selector, "select-list hit-test ignores clicks outside the modal horizontal bounds");
  expect(
      selector_frame.size() == 18 &&
          std::ranges::any_of(selector_frame, [](std::string const& line) { return strip_sgr(line).find("Pick model") != std::string::npos; }) &&
          std::ranges::any_of(selector_frame, [](std::string const& line) { return strip_sgr(line).find("Search: claude") != std::string::npos; }) &&
          std::ranges::any_of(selector_frame,
                              [](std::string const& line) { return strip_sgr(line).find("Choose a provider/model pair ?[31munsafe") != std::string::npos; }) &&
          std::ranges::any_of(selector_frame,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("anthropic") != std::string::npos && visible.find("Claude Sonnet 4.5") != std::string::npos;
                              }) &&
          std::ranges::none_of(selector_frame, [](std::string const& line) { return strip_sgr(line).find("GPT-5.5  current") != std::string::npos; }) &&
          std::ranges::any_of(selector_frame, [](std::string const& line) { return strip_sgr(line).find("Enter choose · Esc cancel") != std::string::npos; }),
      "tui renders reusable select-list modals with search, grouping, filtered rows, and footer hints");

  auto make_model = [](std::string provider, std::string id, std::string name, std::string family, std::optional<bool> reasoning,
                       std::vector<std::string> levels = {}) {
    ava::config::ModelInfo model;
    model.provider_id = std::move(provider);
    model.model_id = std::move(id);
    model.display_name = std::move(name);
    model.family = std::move(family);
    model.context_window_tokens = 200000;
    model.supports_tools = true;
    model.supports_streaming = true;
    model.supports_reasoning = reasoning;
    model.reasoning_levels = std::move(levels);
    return model;
  };
  ava::config::ModelRegistry model_registry{
      .default_provider_id = "openai",
      .default_model_id = "gpt-5.5",
      .models = {
          make_model("openai", "gpt-5.5", "GPT-5.5", "gpt-5", true, {"low", "medium", "high"}),
          make_model("anthropic", "claude-sonnet-4-5", "Claude Sonnet 4.5", "claude-sonnet", false),
          ava::config::ModelInfo{
              .provider_id = "openai", .model_id = "diagnostic-local", .display_name = "Diagnostic Local", .family = "custom", .supports_reasoning = true},
          make_model("unregistered", "remote-model", "Remote Model", "remote", std::nullopt)}};
  auto const model_picker = ava::app::model_selector_view(model_registry, model_registry.models.front(), "Enter choose · Esc cancel");
  expect(model_picker.title == "Select model" && model_picker.selected_item_index == 0 && model_picker.items.size() == 4 && model_picker.items[0].current &&
             model_picker.items[0].enabled && model_picker.items[0].detail.find("levels low/medium/high") != std::string::npos &&
             model_picker.items[2].enabled && model_picker.items[2].badge == "diagnostics" &&
             model_picker.items[2].detail.find("diagnostics") != std::string::npos &&
             model_picker.items[2].detail.find("custom model missing context_window_tokens") != std::string::npos && !model_picker.items[3].enabled &&
             model_picker.items[3].disabled_reason.find("provider unavailable") != std::string::npos,
         "model picker view exposes provider grouping, current marker, capability hints, diagnostics, and disabled providers");
  auto const scoped_all_models =
      ava::app::scoped_model_selector_view(model_registry, model_registry.models.front(), std::nullopt, "Enter toggle · Ctrl+X clear");
  auto const scoped_ordered_models = ava::app::scoped_model_selector_view(
      model_registry, model_registry.models.front(),
      std::optional<std::vector<std::string>>{std::vector<std::string>{"anthropic/claude-sonnet-4-5", "openai/gpt-5.5"}}, "Enter toggle · Ctrl+X clear");
  expect(scoped_all_models.title == "Scoped model cycle" && scoped_all_models.subtitle.find("all configured registered models enabled") != std::string::npos &&
             scoped_all_models.items.size() == 4 && scoped_all_models.items[0].badge == "all-enabled" &&
             scoped_all_models.items[0].description.find("enabled") != std::string::npos && !scoped_all_models.items[3].enabled &&
             scoped_all_models.items[3].disabled_reason.find("provider unavailable") != std::string::npos &&
             scoped_ordered_models.items[0].value == "anthropic/claude-sonnet-4-5" && scoped_ordered_models.items[0].badge == "enabled" &&
             scoped_ordered_models.items[1].value == "openai/gpt-5.5" && scoped_ordered_models.items[2].value == "openai/diagnostic-local" &&
             scoped_ordered_models.items[2].badge == "disabled" && scoped_ordered_models.footer_hint.find("Ctrl+X") != std::string::npos,
         "scoped model selector view marks session cycle scope and preserves explicit enabled order before disabled rows");

  std::vector<ava::session::SessionSummary> session_summaries{
      ava::session::SessionSummary{
          .session_id = "session_beta", .path = "/tmp/ava/sessions/beta.jsonl", .last_updated = "2026-05-06T10:00:00Z", .entry_count = 12},
      ava::session::SessionSummary{
          .session_id = "session_alpha", .path = "/tmp/ava/sessions/alpha.jsonl", .last_updated = "2026-05-06T09:00:00Z", .entry_count = 4},
      ava::session::SessionSummary{
          .session_id = "session_gamma", .path = "/var/tmp/ava/gamma.jsonl", .last_updated = "2026-05-06T11:00:00Z", .entry_count = 20}};
  auto const recent_sessions =
      ava::app::session_selector_view(session_summaries, "session_beta", ava::app::SessionSelectorSort::Recent, "Enter choose · Esc cancel");
  auto by_name_sessions = ava::app::session_selector_view(session_summaries, "session_beta", ava::app::SessionSelectorSort::Name, {});
  auto by_path_sessions = ava::app::session_selector_view(session_summaries, "session_beta", ava::app::SessionSelectorSort::Path, {});
  by_path_sessions.query = "gamma.jsonl";
  auto const path_matches = ava::tui::filter_select_list_items(by_path_sessions);
  expect(ava::app::session_selector_sort_label(ava::app::SessionSelectorSort::Recent) == "recent" &&
             ava::app::next_session_selector_sort(ava::app::SessionSelectorSort::Recent) == ava::app::SessionSelectorSort::Name &&
             ava::app::next_session_selector_sort(ava::app::SessionSelectorSort::Name) == ava::app::SessionSelectorSort::Path &&
             ava::app::next_session_selector_sort(ava::app::SessionSelectorSort::Path) == ava::app::SessionSelectorSort::Recent &&
             recent_sessions.title == "Select session" && recent_sessions.subtitle.find("sort recent") != std::string::npos &&
             recent_sessions.items.size() == 3 && recent_sessions.items[0].value == "session_gamma" && recent_sessions.items[1].current &&
             recent_sessions.selected_item_index == 1 && recent_sessions.items[1].description.find("beta.jsonl") != std::string::npos &&
             recent_sessions.items[1].detail.find("entries 12") != std::string::npos &&
             recent_sessions.items[1].detail.find("updated 2026-05-06T10:00:00Z") != std::string::npos && by_name_sessions.items[0].value == "session_alpha" &&
             by_name_sessions.footer_hint.find("PgUp/PgDn page") != std::string::npos &&
             by_path_sessions.items[0].description.find("/tmp/ava/sessions/alpha.jsonl") != std::string::npos && path_matches.size() == 1 &&
             by_path_sessions.items[path_matches.front()].value == "session_gamma",
         "session selector view sorts by recent/name/path and exposes existing path, time, and entry-count metadata");

  ava::session::SessionTreeIndex session_tree;
  session_tree.current_session_id = "session_child";
  session_tree.roots = {"session_parent"};
  session_tree.leaves = {"session_child"};
  session_tree.current_path = {"session_parent", "session_child"};
  session_tree.sessions = {ava::session::SessionTreeNode{.summary = ava::session::SessionSummary{.session_id = "session_parent",
                                                                                                 .path = "/tmp/ava/sessions/parent.jsonl",
                                                                                                 .last_updated = "2026-05-06T08:00:00Z",
                                                                                                 .entry_count = 6},
                                                         .metadata = ava::session::SessionMetadataView{.name = "Parent session",
                                                                                                       .labels = {"root"},
                                                                                                       .labels_updated = "2026-05-06T08:05:00Z",
                                                                                                       .parent_session_id = {},
                                                                                                       .source_session_id = {},
                                                                                                       .branch_from_entry_id = {},
                                                                                                       .branch_origin = "root",
                                                                                                       .actor = "test"},
                                                         .children = {"session_child"},
                                                         .current = false},
                           ava::session::SessionTreeNode{.summary = ava::session::SessionSummary{.session_id = "session_child",
                                                                                                 .path = "/tmp/ava/sessions/child.jsonl",
                                                                                                 .last_updated = "2026-05-06T10:00:00Z",
                                                                                                 .entry_count = 11},
                                                         .metadata = ava::session::SessionMetadataView{.name = "Review branch",
                                                                                                       .labels = {"review", "ui"},
                                                                                                       .labels_updated = "2026-05-06T10:05:00Z",
                                                                                                       .parent_session_id = "session_parent",
                                                                                                       .source_session_id = "session_parent",
                                                                                                       .branch_from_entry_id = "entry_1",
                                                                                                       .branch_origin = "fork",
                                                                                                       .actor = "rpc"},
                                                         .children = {},
                                                         .current = true}};
  auto tree_sessions = ava::app::session_selector_view(session_tree, ava::app::SessionSelectorSort::Name, {});
  auto tree_sessions_with_label_time = ava::app::session_selector_view(session_tree, ava::app::SessionSelectorSort::Name, {}, false, true, false, true);
  tree_sessions.query = "review";
  auto const tree_matches = ava::tui::filter_select_list_items(tree_sessions);
  expect(tree_sessions.subtitle.find("Session tree") != std::string::npos && tree_sessions.items.size() == 2 &&
             tree_sessions.subtitle.find("label time hidden") != std::string::npos && tree_sessions.items[0].label == "Parent session" &&
             tree_sessions.items[0].detail.find("current path") != std::string::npos &&
             tree_sessions.items[1].label.find("+ Review branch") != std::string::npos && tree_sessions.items[1].current &&
             tree_sessions.items[1].badge == "current" && tree_sessions.items[1].description.find("session_child") != std::string::npos &&
             tree_sessions.items[1].detail.find("origin fork") != std::string::npos &&
             tree_sessions.items[1].detail.find("parent session_parent") != std::string::npos &&
             tree_sessions.items[1].detail.find("labels review, ui") != std::string::npos &&
             tree_sessions.items[1].detail.find("2026-05-06T10:05:00Z") == std::string::npos &&
             tree_sessions_with_label_time.subtitle.find("label time shown") != std::string::npos &&
             tree_sessions_with_label_time.items[1].detail.find("labels review, ui updated 2026-05-06T10:05:00Z") != std::string::npos &&
             tree_matches.size() == 1 && tree_sessions.items[tree_matches.front()].value == "session_child",
         "session selector view exposes tree depth, current path, branch provenance, labels, optional label time, and searchable names");
  auto const parent_target = ava::app::session_selector_parent_target(session_tree, "session_child");
  auto const child_target = ava::app::session_selector_child_target(session_tree, "session_parent", ava::app::SessionSelectorSort::Name);
  expect(parent_target && *parent_target == "session_parent" && child_target && *child_target == "session_child" &&
             !ava::app::session_selector_parent_target(session_tree, "session_parent") &&
             !ava::app::session_selector_child_target(session_tree, "session_child", ava::app::SessionSelectorSort::Name),
         "session selector branch targets resolve parent and first visible child links from tree metadata");

  auto tree_with_unnamed = session_tree;
  tree_with_unnamed.roots.push_back("session_unnamed");
  tree_with_unnamed.sessions.push_back(ava::session::SessionTreeNode{
      .summary =
          ava::session::SessionSummary{
              .session_id = "session_unnamed", .path = "/tmp/ava/sessions/unnamed.jsonl", .last_updated = "2026-05-06T11:00:00Z", .entry_count = 2},
      .metadata =
          ava::session::SessionMetadataView{
              .name = {}, .labels = {}, .parent_session_id = {}, .source_session_id = {}, .branch_from_entry_id = {}, .branch_origin = "root", .actor = "test"},
      .children = {},
      .current = false});
  auto named_only_sessions = ava::app::session_selector_view(tree_with_unnamed, ava::app::SessionSelectorSort::Name, "Ctrl+N show all", true);
  named_only_sessions.query = "unnamed";
  auto const named_only_matches = ava::tui::filter_select_list_items(named_only_sessions);
  expect(named_only_sessions.subtitle.find("named only") != std::string::npos && named_only_sessions.footer_hint.find("Ctrl+N show all") != std::string::npos &&
             named_only_sessions.items.size() == 2 &&
             std::ranges::none_of(named_only_sessions.items, [](ava::tui::SelectListItemView const& item) { return item.value == "session_unnamed"; }) &&
             named_only_matches.empty(),
         "session selector named-only filter hides unnamed sessions while preserving named branch rows");

  auto path_hidden_sessions = ava::app::session_selector_view(tree_with_unnamed, ava::app::SessionSelectorSort::Name, "Ctrl+P show paths", false, false);
  expect(path_hidden_sessions.subtitle.find("paths hidden") != std::string::npos &&
             path_hidden_sessions.footer_hint.find("Ctrl+P show paths") != std::string::npos && path_hidden_sessions.items.size() == 3 &&
             path_hidden_sessions.items[0].description.find(".jsonl") == std::string::npos && path_hidden_sessions.items[1].description == "session_child" &&
             path_hidden_sessions.items[2].description.empty(),
         "session selector path-display toggle hides session file paths while keeping session ids for named rows");

  auto tree_with_archived = tree_with_unnamed;
  tree_with_archived.roots.push_back("session_archived");
  tree_with_archived.sessions.push_back(ava::session::SessionTreeNode{
      .summary =
          ava::session::SessionSummary{
              .session_id = "session_archived", .path = "/tmp/ava/sessions/archived.jsonl", .last_updated = "2026-05-06T12:00:00Z", .entry_count = 3},
      .metadata = ava::session::SessionMetadataView{.name = "Archived branch",
                                                    .labels = {"old"},
                                                    .archived = true,
                                                    .parent_session_id = {},
                                                    .source_session_id = {},
                                                    .branch_from_entry_id = {},
                                                    .branch_origin = "manual",
                                                    .actor = "test"},
      .children = {},
      .current = false});
  auto active_session_selector = ava::app::session_selector_view(tree_with_archived, ava::app::SessionSelectorSort::Name, {}, false, true);
  auto archived_session_selector = ava::app::session_selector_view(tree_with_archived, ava::app::SessionSelectorSort::Name, {}, false, true, true);
  expect(std::ranges::none_of(active_session_selector.items, [](ava::tui::SelectListItemView const& item) { return item.value == "session_archived"; }) &&
             std::ranges::any_of(archived_session_selector.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "session_archived" && item.badge == "archived" && item.detail.find("archived") != std::string::npos;
                                 }) &&
             archived_session_selector.subtitle.find("archived shown") != std::string::npos,
         "session selector hides archived sessions by default and can render them when explicitly requested");

  auto const session_selector_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_beta",
                                                                                           .input = "composer behind session selector",
                                                                                           .status = "selecting session",
                                                                                           .transcript = {},
                                                                                           .select_list = recent_sessions,
                                                                                           .width = 96,
                                                                                           .height = 18});
  expect(std::ranges::any_of(session_selector_frame, [](std::string const& line) { return strip_sgr(line).find("Select session") != std::string::npos; }) &&
             std::ranges::any_of(session_selector_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("session_beta") != std::string::npos && visible.find("beta.jsonl") != std::string::npos;
                                 }),
         "session selector view renders through the reusable select-list modal");

  auto hotkeys_view = ava::tui::hotkeys_select_list_view(ava::tui::default_key_bindings());
  hotkeys_view.query = "mode";
  auto const hotkeys_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                  .provider = "openai",
                                                                                  .model = "gpt-5.5",
                                                                                  .session_id = "session_test",
                                                                                  .input = "composer behind hotkeys",
                                                                                  .status = "hotkeys opened",
                                                                                  .transcript = {},
                                                                                  .select_list = hotkeys_view,
                                                                                  .width = 92,
                                                                                  .height = 18});
  expect(hotkeys_view.title == "Keybindings" && hotkeys_view.subtitle.find("$XDG_CONFIG_HOME/ava/keybinds.json") != std::string::npos &&
             hotkeys_view.subtitle.find("Enter drafts /keybindings set") != std::string::npos &&
             hotkeys_view.subtitle.find("/reload keybindings") != std::string::npos && hotkeys_view.footer_hint.find("Enter draft edit") != std::string::npos &&
             std::ranges::any_of(hotkeys_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "mode_toggle" && item.detail.find("Tab") != std::string::npos && item.badge == "shared key";
                                 }) &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("Keybindings") != std::string::npos; }) &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("/reload keybindings") != std::string::npos; }) &&
             std::ranges::any_of(hotkeys_frame, [](std::string const& line) { return strip_sgr(line).find("mode_toggle") != std::string::npos; }),
         "keybindings view exposes active bindings, edit drafting, config/reload guidance, and context-shared keys");

  ava::tui::set_tui_config_theme(std::nullopt);
  auto settings_key_bindings = ava::tui::default_key_bindings();
  auto const parsed_settings_key_bindings = ava::tui::parse_key_bindings_json("{\"tui.editor.cursorLeft\":\"Alt+H\",\"app.tools.expand\":\"Ctrl+O\"}");
  expect(parsed_settings_key_bindings.has_value(), "settings view test keybindings parse through the production loader");
  if (parsed_settings_key_bindings)
    settings_key_bindings = *parsed_settings_key_bindings;
  auto const settings_view = ava::tui::settings_select_list_view(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .token_status = "1.3k (0.7%)",
                                 .reasoning_status = "low",
                                 .transcript = {},
                                 .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                      .mode = "build",
                                                                      .provider = "openai",
                                                                      .model = "gpt-5.5",
                                                                      .workspace = "/workspace/project",
                                                                      .git_branch = "develop",
                                                                      .version = "0.32",
                                                                      .token_status = "1.3k (0.7%)",
                                                                      .reasoning_status = "low",
                                                                      .context_source_count = 2,
                                                                      .session_path = "/tmp/ava/session_test.jsonl",
                                                                      .session_entry_count = 42},
                                 .project_trust = ava::tui::ProjectTrustSnapshot{.decision = "trusted",
                                                                                 .project_resources = "enabled",
                                                                                 .workspace = "/workspace/project",
                                                                                 .matched_path = "/workspace/project",
                                                                                 .trust_file = "/tmp/ava/project-trust.json",
                                                                                 .protected_resource_count = 3}},
      settings_key_bindings);
  auto const settings_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "",
                                                                                   .status = "settings opened",
                                                                                   .transcript = {},
                                                                                   .select_list = settings_view,
                                                                                   .width = 96,
                                                                                   .height = 20});
  expect(std::ranges::any_of(
             settings_view.items,
             [](ava::tui::SelectListItemView const& item) { return item.label == "Theme" && item.description == "ava-dark" && item.badge == "built-in"; }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "theme:dark" && item.label == "Theme dark" && item.detail == "persist to display.json" &&
                                          item.badge == "current" && item.current;
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.value == "theme:light" && item.label == "Theme light" && item.detail == "persist to display.json" &&
                                          item.badge == "select" && !item.current;
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Current directory" && item.description == "/workspace/project" && item.detail == "project";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Project trust" && item.description == "trusted" && item.detail == "project resources enabled" &&
                                          item.badge == "trusted";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Protected resources" && item.description == "3 protected project resources" &&
                                          item.detail == "matched /workspace/project";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Trust status" && item.value == "settings:trust.status" && item.description == "/trust status" &&
                                          item.badge == "status";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Trust project" && item.value == "settings:trust.project" &&
                                          item.detail == "Enter runs /trust project" && item.badge == "trust";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Deny project" && item.value == "settings:trust.deny" && item.detail == "Enter runs /trust deny" &&
                                          item.badge == "deny";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Clear trust decision" && item.value == "settings:trust.clear" &&
                                          item.detail == "Enter runs /trust clear" && item.badge == "clear";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Image preview" && !item.description.empty() && !item.detail.empty() && !item.badge.empty();
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings" && item.value == "settings:keybindings.open" &&
                                          item.description.find("active actions") != std::string::npos && item.detail == "Enter opens active bindings" &&
                                          item.badge == "open";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings file" && item.value == "settings:keybindings.validate" &&
                                          item.description == "$XDG_CONFIG_HOME/ava/keybinds.json" &&
                                          item.detail == "Enter validates with /keybindings validate" && item.badge == "validate";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings edit" && item.value == "settings:keybindings.edit" &&
                                          item.description == "/keybindings set <action> <key>" &&
                                          item.detail == "Enter drafts the edit command; reset removes one override" && item.badge == "draft";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Keybindings reload" && item.value == "settings:keybindings.reload" &&
                                          item.description == "/reload keybindings" && item.detail == "Enter applies valid keybinds.json edits" &&
                                          item.badge == "live";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Model selector" && item.value == "settings:models.open" && item.description == "openai/gpt-5.5" &&
                                          item.detail == "Enter opens /models selector" && item.badge == "open";
                                 }) &&
             std::ranges::any_of(settings_view.items,
                                 [](ava::tui::SelectListItemView const& item) {
                                   return item.label == "Model cycle scope" && item.value == "settings:models.scoped" &&
                                          item.description == "Ctrl+P scoped cycle" && item.detail.find("/scoped-models") != std::string::npos &&
                                          item.detail.find("Ctrl+S") != std::string::npos && item.badge == "open";
                                 }) &&
             std::ranges::any_of(settings_frame, [](std::string const& line) { return strip_sgr(line).find("Settings") != std::string::npos; }) &&
             std::ranges::any_of(settings_frame, [](std::string const& line) { return strip_sgr(line).find("ava-dark") != std::string::npos; }),
         "settings view exposes runtime, workspace, built-in theme status, and selectable theme rows");

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar light_theme_guard("AVA_TUI_THEME", "light");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const light_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(std::ranges::any_of(light_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "ava-light" && item.detail == "built-in light ncurses token palette" &&
                                        item.badge == "AVA_TUI_THEME";
                               }),
           "settings view reports process-selected built-in light theme from AVA_TUI_THEME");
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme("light");
    auto const configured_light_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(std::ranges::any_of(configured_light_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "ava-light" && item.detail == "built-in light ncurses token palette" &&
                                        item.badge == "display.json";
                               }) &&
               std::ranges::any_of(
                   configured_light_settings_view.items,
                   [](ava::tui::SelectListItemView const& item) { return item.value == "theme:light" && item.badge == "current" && item.current; }),
           "settings view reports persisted built-in light theme from display.json");
    ava::tui::set_tui_config_theme(std::nullopt);
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    auto custom_theme = ava::tui::TuiCustomTheme{
        .name = "sunrise",
        .path = "/tmp/ava/sunrise.json",
        .palette =
            ava::tui::TuiThemePalette{
                .text = -1, .muted = 242, .success = 34, .warning = 220, .error = 196, .accent = 39, .screen_bg = 255, .composer_bg = 236},
        .revision = "test-custom-theme"};
    ava::tui::set_tui_config_theme("sunrise", custom_theme);
    auto const custom_settings_view = ava::tui::settings_select_list_view(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .token_status = "1.3k (0.7%)",
                                   .reasoning_status = "low",
                                   .transcript = {},
                                   .custom_themes = {ava::tui::ThemeOptionItem{.name = "sunrise", .detail = "/tmp/ava/sunrise.json"}},
                                   .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                        .mode = "build",
                                                                        .provider = "openai",
                                                                        .model = "gpt-5.5",
                                                                        .workspace = "/workspace/project",
                                                                        .git_branch = "develop",
                                                                        .version = "0.32",
                                                                        .token_status = "1.3k (0.7%)",
                                                                        .reasoning_status = "low",
                                                                        .context_source_count = 2,
                                                                        .session_path = "/tmp/ava/session_test.jsonl",
                                                                        .session_entry_count = 42}});
    expect(std::ranges::any_of(custom_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "sunrise" && item.detail.find("sunrise.json") != std::string::npos &&
                                        item.badge == "display.json";
                               }) &&
               std::ranges::any_of(custom_settings_view.items,
                                   [](ava::tui::SelectListItemView const& item) {
                                     return item.value == "theme:sunrise" && item.label == "Theme sunrise" && item.description == "custom theme" &&
                                            item.detail.find("sunrise.json") != std::string::npos && item.badge == "current" && item.current;
                                   }),
           "settings view reports and exposes a selected custom TUI theme from display.json");
    ava::tui::set_tui_config_theme(std::nullopt);
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "0;15");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const terminal_light_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(std::ranges::any_of(terminal_light_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "ava-light" &&
                                        item.detail == "terminal background appears light from COLORFGBG" && item.badge == "COLORFGBG";
                               }) &&
               std::ranges::any_of(
                   terminal_light_settings_view.items,
                   [](ava::tui::SelectListItemView const& item) { return item.value == "theme:light" && item.badge == "current" && item.current; }),
           "settings view reports terminal-background light theme inferred from COLORFGBG");
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "15;0");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const terminal_dark_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    expect(
        std::ranges::any_of(terminal_dark_settings_view.items,
                            [](ava::tui::SelectListItemView const& item) {
                              return item.label == "Theme" && item.description == "ava-dark" &&
                                     item.detail == "terminal background appears dark from COLORFGBG" && item.badge == "COLORFGBG";
                            }) &&
            std::ranges::any_of(terminal_dark_settings_view.items,
                                [](ava::tui::SelectListItemView const& item) { return item.value == "theme:dark" && item.badge == "current" && item.current; }),
        "settings view reports terminal-background dark theme inferred from COLORFGBG");
  }

  {
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "");
    ScopedEnvVar plain_theme_guard("AVA_TUI_THEME", "plain");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const env_plain_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    auto const env_plain_settings_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                               .provider = "openai",
                                                                                               .model = "gpt-5.5",
                                                                                               .session_id = "session_test",
                                                                                               .input = "",
                                                                                               .status = "settings opened",
                                                                                               .transcript = {},
                                                                                               .select_list = env_plain_settings_view,
                                                                                               .width = 96,
                                                                                               .height = 20});
    expect(std::ranges::any_of(env_plain_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "plain" && item.detail == "AVA_TUI_THEME=plain disables ANSI styling" &&
                                        item.badge == "AVA_TUI_THEME";
                               }) &&
               std::ranges::all_of(env_plain_settings_frame, [](std::string const& line) { return line.find("\x1b[") == std::string::npos; }),
           "AVA_TUI_THEME=plain selects the same no-ANSI rendering path as NO_COLOR without requiring NO_COLOR");
  }

  {
    ScopedEnvVar requested_theme_guard("AVA_TUI_THEME", "light");
    ScopedEnvVar no_color_settings_guard("NO_COLOR", "1");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "0;15");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto const plain_settings_view =
        ava::tui::settings_select_list_view(ava::tui::ComposerSnapshot{.mode = "build",
                                                                       .provider = "openai",
                                                                       .model = "gpt-5.5",
                                                                       .session_id = "session_test",
                                                                       .input = "",
                                                                       .status = "ready",
                                                                       .token_status = "1.3k (0.7%)",
                                                                       .reasoning_status = "low",
                                                                       .transcript = {},
                                                                       .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                            .mode = "build",
                                                                                                            .provider = "openai",
                                                                                                            .model = "gpt-5.5",
                                                                                                            .workspace = "/workspace/project",
                                                                                                            .git_branch = "develop",
                                                                                                            .version = "0.32",
                                                                                                            .token_status = "1.3k (0.7%)",
                                                                                                            .reasoning_status = "low",
                                                                                                            .context_source_count = 2,
                                                                                                            .session_path = "/tmp/ava/session_test.jsonl",
                                                                                                            .session_entry_count = 42}});
    auto const plain_settings_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_test",
                                                                                           .input = "",
                                                                                           .status = "settings opened",
                                                                                           .transcript = {},
                                                                                           .select_list = plain_settings_view,
                                                                                           .width = 96,
                                                                                           .height = 20});
    expect(std::ranges::any_of(plain_settings_view.items,
                               [](ava::tui::SelectListItemView const& item) {
                                 return item.label == "Theme" && item.description == "plain" && item.detail == "NO_COLOR disables ANSI styling" &&
                                        item.badge == "NO_COLOR";
                               }) &&
               std::ranges::any_of(plain_settings_frame, [](std::string const& line) { return line.find("plain") != std::string::npos; }) &&
               std::ranges::all_of(plain_settings_frame, [](std::string const& line) { return line.find("\x1b[") == std::string::npos; }),
           "settings view reports active NO_COLOR plain display mode without ANSI styling");
  }

  auto const multiline_input = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "first\nsecond",
                                                                                    .status = "ready",
                                                                                    .transcript = {},
                                                                                    .width = 50,
                                                                                    .height = 8});
  expect(multiline_input.size() == 8 && strip_sgr(multiline_input[5]).starts_with("│  first") && strip_sgr(multiline_input[6]).starts_with("│  second") &&
             strip_sgr(multiline_input[7]).starts_with("│  GPT-5.5") && multiline_input[4].find("\x1b[48;2;26;31;46m") == std::string::npos &&
             std::ranges::none_of(multiline_input, [](std::string const& line) { return strip_sgr(line).find("❯") != std::string::npos; }),
         "tui renders multiline input plus one footer with the same three-column prefix and no surface padding");
  auto const empty_composer_height = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                          .provider = "openai",
                                                                                          .model = "gpt-5.5",
                                                                                          .session_id = "session_test",
                                                                                          .input = "",
                                                                                          .status = "ready",
                                                                                          .transcript = {},
                                                                                          .width = 50,
                                                                                          .height = 12});
  auto const grown_composer_height = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                          .provider = "openai",
                                                                                          .model = "gpt-5.5",
                                                                                          .session_id = "session_test",
                                                                                          .input = "one\ntwo\nthree\nfour\nfive",
                                                                                          .status = "ready",
                                                                                          .transcript = {},
                                                                                          .width = 50,
                                                                                          .height = 12});
  auto const composer_bg_rows = [](std::vector<std::string> const& rendered) {
    return static_cast<std::size_t>(
        std::ranges::count_if(rendered, [](std::string const& line) { return line.find("\x1b[48;2;26;31;46m") != std::string::npos; }));
  };
  expect(composer_bg_rows(grown_composer_height) > composer_bg_rows(empty_composer_height) &&
             std::ranges::any_of(grown_composer_height, [](std::string const& line) { return strip_sgr(line).find("│  five") != std::string::npos; }),
         "tui composer grows with multiline input and keeps the latest line visible");
  auto const tall_draft = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                               .provider = "openai",
                                                                               .model = "gpt-5.5",
                                                                               .session_id = "session_test",
                                                                               .input = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine",
                                                                               .status = "ready",
                                                                               .transcript = {},
                                                                               .width = 70,
                                                                               .height = 12});
  expect(std::ranges::none_of(tall_draft, [](std::string const& line) { return strip_sgr(line).find("draft +") != std::string::npos; }) &&
             std::ranges::any_of(tall_draft, [](std::string const& line) { return strip_sgr(line).find("│  nine") != std::string::npos; }) &&
             std::ranges::none_of(tall_draft, [](std::string const& line) { return strip_sgr(line).find("│  one") != std::string::npos; }),
         "tui composer hides draft overflow text while keeping the latest draft line visible");
  auto const scrolled_draft = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine",
                                                                                   .status = "ready",
                                                                                   .transcript = {},
                                                                                   .width = 70,
                                                                                   .height = 12,
                                                                                   .input_cursor = std::string::npos,
                                                                                   .sidebar = std::nullopt,
                                                                                   .draft_scroll_offset = 2});
  expect(std::ranges::any_of(scrolled_draft, [](std::string const& line) { return strip_sgr(line).find("│  one") != std::string::npos; }) &&
             std::ranges::none_of(scrolled_draft, [](std::string const& line) { return strip_sgr(line).find("│  nine") != std::string::npos; }) &&
             std::ranges::none_of(scrolled_draft, [](std::string const& line) { return strip_sgr(line).find("draft +") != std::string::npos; }),
         "tui composer draft scroll offset shows older draft lines without footer overflow text");

  std::vector<ava::tui::TranscriptItem> many_items;
  for (int index = 0; index < 20; ++index)
  {
    many_items.push_back(ava::tui::TranscriptItem{.label = "line", .text = "item " + std::to_string(index)});
  }
  auto const scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                             .provider = "openai",
                                                                             .model = "gpt-5.5",
                                                                             .session_id = "session_test",
                                                                             .input = "",
                                                                             .status = "ready",
                                                                             .transcript = many_items,
                                                                             .width = 40,
                                                                             .height = 12});
  expect(std::ranges::any_of(scrolled, [](std::string const& line) { return line.find("item 19") != std::string::npos; }) &&
             std::ranges::none_of(scrolled, [](std::string const& line) { return line.find("lines hidden") != std::string::npos; }) &&
             std::ranges::none_of(scrolled, [](std::string const& line) { return line.find("item 0") != std::string::npos; }),
         "tui transcript viewport keeps newest lines without hidden-line banners");

  auto const scrolled_up = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                .provider = "openai",
                                                                                .model = "gpt-5.5",
                                                                                .session_id = "session_test",
                                                                                .input = "",
                                                                                .status = "ready",
                                                                                .transcript = many_items,
                                                                                .selected_slash_command_index = 0,
                                                                                .transcript_scroll_offset = 4,
                                                                                .width = 80,
                                                                                .height = 12});
  expect(std::ranges::none_of(scrolled_up, [](std::string const& line) { return line.find("lines hidden") != std::string::npos; }) &&
             std::ranges::any_of(scrolled_up, [](std::string const& line) { return line.find("item 15") != std::string::npos; }) &&
             std::ranges::none_of(scrolled_up, [](std::string const& line) { return line.find("item 19") != std::string::npos; }),
         "tui transcript viewport supports an explicit scroll offset");

  auto const detached_scroll = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "",
                                                                                    .status = "ready",
                                                                                    .transcript = many_items,
                                                                                    .transcript_scroll_offset = 4,
                                                                                    .transcript_new_output_count = 3,
                                                                                    .width = 80,
                                                                                    .height = 12});
  expect(std::ranges::any_of(detached_scroll,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("scrollback detached") != std::string::npos && visible.find("+3 updates below") != std::string::npos &&
                                      visible.find("jump_to_bottom") != std::string::npos;
                             }),
         "tui transcript scrollback shows a detached/new-output indicator with the jump-to-bottom action name");

  auto const wrapped_transcript = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                                                         .text = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"}},
                                 .selected_slash_command_index = 0,
                                 .transcript_scroll_offset = 1,
                                 .width = 60,
                                 .height = 8});
  auto const wrapped_latest = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava",
                                                                         .text = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                                                                                 "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"}},
                                 .selected_slash_command_index = 0,
                                 .transcript_scroll_offset = 0,
                                 .width = 60,
                                 .height = 8});
  expect(std::ranges::none_of(wrapped_transcript, [](std::string const& line) { return strip_sgr(line).find("lines hidden") != std::string::npos; }) &&
             wrapped_transcript != wrapped_latest,
         "tui transcript viewport wraps long transcript text before applying scroll offset without hidden-line banners");

  std::vector<ava::tui::TranscriptItem> mixed_items;
  for (int index = 0; index < 8; ++index)
  {
    mixed_items.push_back(ava::tui::TranscriptItem{.label = "line", .text = "old " + std::to_string(index)});
  }
  mixed_items.push_back(ava::tui::TranscriptItem{
      .tool = ava::tui::ToolTimelineItem{
          .status = ava::tui::ToolTimelineStatus::Success, .name = "grep", .argument_summary = "needle", .result_summary = "2 matches"}});
  mixed_items.push_back(ava::tui::TranscriptItem{.label = "ava", .text = "done"});
  auto const mixed_scrolled = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                   .provider = "openai",
                                                                                   .model = "gpt-5.5",
                                                                                   .session_id = "session_test",
                                                                                   .input = "",
                                                                                   .status = "ready",
                                                                                   .transcript = mixed_items,
                                                                                   .width = 60,
                                                                                   .height = 12});
  std::string mixed_visible;
  for (auto const& line : mixed_scrolled)
  {
    mixed_visible += strip_sgr(line);
    mixed_visible += '\n';
  }
  expect(mixed_visible.find("lines hidden") == std::string::npos && mixed_visible.find("[+]") != std::string::npos &&
             mixed_visible.find("2 matches") != std::string::npos && mixed_visible.find("done") != std::string::npos &&
             mixed_visible.find("AVA") == std::string::npos && mixed_visible.find("old 0") == std::string::npos,
         "tui transcript viewport scrolls mixed text and tool-card lines together without hidden-line banners");

  auto const multiline = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                              .provider = "openai",
                                                                              .model = "gpt-5.5",
                                                                              .session_id = "session_test",
                                                                              .input = "",
                                                                              .status = "ready",
                                                                              .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "one\ntwo"}},
                                                                              .width = 80,
                                                                              .height = 14});
  expect(std::ranges::any_of(multiline,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("one") != std::string::npos;
                             }) &&
             std::ranges::any_of(multiline,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("two") != std::string::npos;
                                 }),
         "tui renders multiline assistant transcript content inside the message block");

  auto const tool_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "read_file",
                                                                                                            .argument_summary = "path=note.txt\x1b[31m",
                                                                                                            .result_summary = "read lines 1-12/12"}}},
                                 .width = 80,
                                 .height = 10});
  expect(std::ranges::any_of(tool_card,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos && visible.find("read_file") != std::string::npos &&
                                      visible.find("path=note.txt") != std::string::npos;
                             }) &&
             std::ranges::any_of(tool_card,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("read lines 1-12/12") != std::string::npos;
                                 }) &&
             std::ranges::any_of(tool_card,
                                 [](std::string const& line) {
                                   return line.find("\x1b[38;2;52;211;153m[+]") != std::string::npos &&
                                          line.find("\x1b[1m\x1b[38;2;77;158;246mread_file") != std::string::npos &&
                                          line.find("\x1b[38;2;139;149;165mpath=note.txt") != std::string::npos;
                                 }),
         "tui renders compact styled tool timeline cards");
  expect(std::ranges::none_of(tool_card, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui tool card rendering removes untrusted raw sgr escape sequences");

  auto permission_tool_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                         .name = "bash",
                                                         .argument_summary = "command=git push origin main",
                                                         .result_summary = "permission denied",
                                                         .arguments_json = "{\"command\":\"git push origin main\"}",
                                                         .result_json = "{\"tool\":\"bash\",\"exit_code\":126}",
                                                         .call_id = "call_permission",
                                                         .lifecycle = ava::tui::ToolLifecycleState::Error,
                                                         .permission_request_ids = {"permreq_push"},
                                                         .permissions = {ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_push",
                                                                                                           .resolver_request_id = "permission_1",
                                                                                                           .decision = "deny",
                                                                                                           .operation = "bash",
                                                                                                           .tool_name = "bash",
                                                                                                           .risk = "critical",
                                                                                                           .reason = "command permission denied\x1b[31m",
                                                                                                           .target = "",
                                                                                                           .command = "<redacted one-shot command>",
                                                                                                           .resolution_reason = "selected deny"}}};
  auto const compact_permission_card =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.tool = permission_tool_item}},
                                                           .width = 72,
                                                           .height = 12});
  expect(std::ranges::any_of(compact_permission_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("permission deny") != std::string::npos && visible.find("risk critical") != std::string::npos &&
                                      visible.find("reason command permission d") != std::string::npos;
                             }) &&
             std::ranges::none_of(compact_permission_card, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(compact_permission_card, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui compact tool cards preserve linked permission decision, risk, and reason without leaking raw escapes");

  permission_tool_item.details_visible = true;
  auto const expanded_permission_card =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.tool = permission_tool_item}},
                                                           .width = 88,
                                                           .height = 16});
  expect(std::ranges::any_of(expanded_permission_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("permission: deny") != std::string::npos && visible.find("id permreq_push") != std::string::npos &&
                                      visible.find("resolver permission_1") != std::string::npos;
                             }) &&
             std::ranges::any_of(expanded_permission_card,
                                 [](std::string const& line) { return strip_sgr(line).find("command: git push origin main") != std::string::npos; }) &&
             std::ranges::any_of(
                 expanded_permission_card,
                 [](std::string const& line) { return strip_sgr(line).find("inspect: /permissions audit show permreq_push") != std::string::npos; }) &&
             std::ranges::any_of(
                 expanded_permission_card,
                 [](std::string const& line) { return strip_sgr(line).find("diagnose: /permissions diagnose permreq_push") != std::string::npos; }) &&
             std::ranges::all_of(expanded_permission_card, [](std::string const& line) { return visible_columns(line) <= 88; }),
         "tui expanded tool cards expose permission audit ids, reviewed tool arguments, and follow-up commands on demand");

  {
    ScopedEnvVar no_color_permission_guard("NO_COLOR", "1");
    auto const plain_narrow_permission_card =
        ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                             .provider = "openai",
                                                             .model = "gpt-5.5",
                                                             .session_id = "session_test",
                                                             .input = "",
                                                             .status = "ready",
                                                             .transcript = {ava::tui::TranscriptItem{.tool = permission_tool_item}},
                                                             .width = 56,
                                                             .height = 24,
                                                             .tool_details_visible = true});
    auto plain_permission_text = std::string{};
    for (auto const& line : plain_narrow_permission_card)
    {
      plain_permission_text += line;
      plain_permission_text += '\n';
    }
    auto const plain_permission_accessible =
        std::ranges::all_of(plain_narrow_permission_card,
                            [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 56; }) &&
        plain_permission_text.find("[x] bash") != std::string::npos && plain_permission_text.find("permission: deny") != std::string::npos &&
        plain_permission_text.find("risk: critical") != std::string::npos && plain_permission_text.find("id: permreq_push") != std::string::npos &&
        plain_permission_text.find("reason: command permission denied") != std::string::npos &&
        plain_permission_text.find("command: <redacted one-shot command>") != std::string::npos &&
        plain_permission_text.find("inspect: /permissions audit show permreq_push") != std::string::npos &&
        plain_permission_text.find("diagnose: /permissions diagnose permreq_push") != std::string::npos;
    expect(plain_permission_accessible, "tui plain narrow permission cards keep critical state readable without color");
  }

  auto const grouped_context_tools = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "glob",
                                                                                                            .argument_summary = "pattern=src/**/*.cpp",
                                                                                                            .result_summary = "12 files"}},
                                                ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "grep",
                                                                                                            .argument_summary = "pattern=TODO",
                                                                                                            .result_summary = "3 matches"}},
                                                ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "read_file",
                                                                                                            .argument_summary = "path=src/main.cpp",
                                                                                                            .result_summary = "read lines 1-40"}}},
                                 .width = 96,
                                 .height = 16});
  expect(std::ranges::count_if(grouped_context_tools,
                               [](std::string const& line) { return strip_sgr(line).find("context gathering · 3 tools") != std::string::npos; }) == 1 &&
             std::ranges::any_of(grouped_context_tools, [](std::string const& line) { return strip_sgr(line).find("glob") != std::string::npos; }) &&
             std::ranges::any_of(grouped_context_tools, [](std::string const& line) { return strip_sgr(line).find("read_file") != std::string::npos; }),
         "tui groups consecutive context-gathering tool cards with a single readable heading while keeping details");

  auto const empty_tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success, .name = "", .argument_summary = "", .result_summary = ""}}},
      .width = 40,
      .height = 8});
  expect(std::ranges::any_of(empty_tool_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("[+]") != std::string::npos && visible.find("unknown") != std::string::npos;
                             }) &&
             std::ranges::all_of(empty_tool_card, [](std::string const& line) { return visible_columns(line) <= 40; }),
         "tui renders empty tool-card fields with a safe fallback name");

  auto visible_text = [](std::vector<std::string> const& lines) {
    std::string text;
    for (auto const& line : lines)
    {
      text += strip_sgr(line);
      text += '\n';
    }
    return text;
  };
  auto const webfetch_item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                        .name = "webfetch",
                                                        .argument_summary = "url=https://example.test/docs\x1b[31m",
                                                        .result_summary = "fetched text/plain",
                                                        .result_json = "{\"content_type\":\"text/plain\",\"content\":\"alpha\\nbeta\\ngamma\"}",
                                                        .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                        .truncated = true,
                                                        .output_lines = 2,
                                                        .total_lines = 3};
  auto const webfetch_card = ava::tui::detail::render_tool_card(webfetch_item, 72, true);
  auto const webfetch_text = visible_text(webfetch_card);
  auto const webfetch_copy = ava::tui::detail::tool_card_copy_text(webfetch_item);
  expect(webfetch_text.find("webfetch") != std::string::npos && webfetch_text.find("args: url=https://example.test/docs") != std::string::npos,
         "tui non-shell webfetch cards preserve the tool name and sanitized arguments");
  expect(webfetch_text.find("output:") != std::string::npos && webfetch_text.find("alpha") != std::string::npos,
         "tui non-shell webfetch cards render expanded content previews");
  expect(webfetch_copy.find("output:\n  alpha\n  beta\n  gamma") != std::string::npos &&
             webfetch_copy.find("truncation: truncated 2/3 lines") != std::string::npos && webfetch_copy.find("\x1b[") == std::string::npos,
         "tui non-shell webfetch copy text includes preview, truncation, and no ANSI styling");
  expect(std::ranges::none_of(webfetch_card, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(webfetch_card, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui non-shell webfetch cards remove raw escapes and stay within width");

  auto const non_shell_cards =
      std::vector<ava::tui::ToolTimelineItem>{ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                                         .name = "websearch",
                                                                         .argument_summary = "query=terminal renderer",
                                                                         .result_summary = "provider error",
                                                                         .result_json = "{\"error\":\"rate limited\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Error},
                                              ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                         .name = "skill",
                                                                         .argument_summary = "name=frontend-review",
                                                                         .result_summary = "loaded skill instructions",
                                                                         .result_json = "{\"preview\":\"Use semantic landmarks\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                         .permission_request_ids = {"permreq_skill"}},
                                              ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                         .name = "question",
                                                                         .argument_summary = "choose deployment target",
                                                                         .result_summary = "answered: staging",
                                                                         .result_json = "{\"answer\":\"staging\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Complete},
                                              ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                         .name = "lsp_diagnostics",
                                                                         .argument_summary = "path=src/main.cpp",
                                                                         .result_summary = "2 diagnostics",
                                                                         .result_json = "{\"output\":\"line 12 warning\\nline 18 error\"}",
                                                                         .lifecycle = ava::tui::ToolLifecycleState::Complete}};
  for (auto const& item : non_shell_cards)
  {
    auto const card = ava::tui::detail::render_tool_card(item, 36, true);
    auto const text = visible_text(card);
    auto const copy = ava::tui::detail::tool_card_copy_text(item);
    expect(text.find(item.name) != std::string::npos && text.find("shell status") == std::string::npos &&
               copy.find("tool: " + item.name) != std::string::npos && copy.find("shell status") == std::string::npos &&
               copy.find("\x1b[") == std::string::npos && std::ranges::all_of(card, [](std::string const& line) { return visible_columns(line) <= 36; }),
           "tui non-shell tool cards render and copy safely on narrow terminals for " + item.name);
  }
  auto const skill_permission_copy = ava::tui::detail::tool_card_permission_copy_text(non_shell_cards[1]);
  expect(skill_permission_copy.empty(), "tui permission copy text stays empty when only unresolved permission ids are present");
  auto const expanded_skill_card = ava::tui::detail::render_tool_card(non_shell_cards[1], 48, true);
  auto const expanded_skill_text = visible_text(expanded_skill_card);
  expect(expanded_skill_text.find("permission: checked") != std::string::npos && expanded_skill_text.find("id: permreq_skill") != std::string::npos,
         "tui expanded non-shell tool cards keep unresolved permission ids visible");

  {
    ScopedEnvVar no_color_tool_guard("NO_COLOR", "1");
    auto const plain_non_shell_card = ava::tui::render_composer(
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_test",
                                   .input = "",
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.tool = webfetch_item}, ava::tui::TranscriptItem{.tool = non_shell_cards[3]}},
                                   .width = 40,
                                   .height = 30,
                                   .tool_details_visible = true});
    auto const plain_text = visible_text(plain_non_shell_card);
    expect(std::ranges::all_of(plain_non_shell_card,
                               [](std::string const& line) { return line.find('\x1b') == std::string::npos && visible_columns(line) <= 40; }) &&
               plain_text.find("webfetch") != std::string::npos && plain_text.find("lsp_diagnostics") != std::string::npos &&
               plain_text.find("output:") != std::string::npos,
           "tui plain narrow non-shell tool cards keep names and output readable without color");
  }

  auto const running_error_cards = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=build\x1b[31m now",
                                                                                 .result_summary = ""}},
                     ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                                                 .name = "write_file",
                                                                                 .argument_summary = std::string(120, 'a') + "\x1b[31m",
                                                                                 .result_summary = "error: denied\x1b[31m"}}},
      .width = 60,
      .height = 14});
  expect(std::ranges::any_of(running_error_cards,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[~]") != std::string::npos && visible.find("bash") != std::string::npos &&
                                      visible.find("command=build?") != std::string::npos;
                             }) &&
             std::ranges::any_of(running_error_cards,
                                 [](std::string const& line) {
                                   auto visible = strip_sgr(line);
                                   return visible.find("[x]") != std::string::npos && visible.find("write_file") != std::string::npos;
                                 }) &&
             std::ranges::all_of(running_error_cards, [](std::string const& line) { return visible_columns(line) <= 60; }),
         "tui renders running/error tool cards with sanitized truncated summaries");
  expect(std::ranges::none_of(running_error_cards, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }),
         "tui running/error tool cards remove untrusted raw sgr escape sequences");
  expect(std::ranges::any_of(running_error_cards, [](std::string const& line) { return line.find("\x1b[38;2;251;191;36m[~]") != std::string::npos; }) &&
             std::ranges::any_of(running_error_cards, [](std::string const& line) { return line.find("\x1b[38;2;248;113;113m[x]") != std::string::npos; }),
         "tui emits trusted sgr status colors for running and error tool cards");

  auto const canceled_tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Canceled,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=sleep 30",
                                                                                 .result_summary = "stopped by user",
                                                                                 .arguments_json = "{\"command\":\"sleep 30\"}",
                                                                                 .result_json = "{\"tool\":\"bash\",\"canceled\":true}",
                                                                                 .lifecycle = ava::tui::ToolLifecycleState::Canceled}}},
      .width = 64,
      .height = 16,
      .tool_details_visible = true});
  expect(std::ranges::any_of(canceled_tool_card,
                             [](std::string const& line) {
                               auto visible = strip_sgr(line);
                               return visible.find("[-]") != std::string::npos && visible.find("bash") != std::string::npos &&
                                      visible.find("canceled") != std::string::npos;
                             }) &&
             std::ranges::any_of(canceled_tool_card, [](std::string const& line) { return strip_sgr(line).find("cancel: stopped") != std::string::npos; }) &&
             std::ranges::none_of(canceled_tool_card, [](std::string const& line) { return strip_sgr(line).find("[x]") != std::string::npos; }) &&
             std::ranges::all_of(canceled_tool_card, [](std::string const& line) { return visible_columns(line) <= 64; }),
         "tui renders canceled tool cards as a distinct non-error status");
  auto const canceled_copy_text = ava::tui::detail::tool_card_copy_text(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Canceled,
                                                                                                   .name = "bash",
                                                                                                   .argument_summary = "command=sleep 30",
                                                                                                   .result_summary = "stopped by user",
                                                                                                   .arguments_json = "{\"command\":\"sleep 30\"}",
                                                                                                   .result_json = "{\"tool\":\"bash\",\"canceled\":true}",
                                                                                                   .lifecycle = ava::tui::ToolLifecycleState::Canceled});
  expect(canceled_copy_text.find("status: canceled") != std::string::npos && canceled_copy_text.find("lifecycle: canceled") != std::string::npos &&
             canceled_copy_text.find("\x1b[") == std::string::npos,
         "tui copy text preserves canceled tool state without ANSI styling");

  auto const detailed_tool_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=cmake --build build",
                                                                                 .result_summary = "line one line two line three line four"}}},
      .width = 48,
      .height = 12,
      .tool_details_visible = true});
  expect(std::ranges::any_of(detailed_tool_card, [](std::string const& line) { return strip_sgr(line).find("args: command=cmake") != std::string::npos; }) &&
             std::ranges::any_of(detailed_tool_card, [](std::string const& line) { return strip_sgr(line).find("result: line one") != std::string::npos; }) &&
             std::ranges::all_of(detailed_tool_card, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui expands tool cards into sanitized argument and result detail rows when details are enabled");

  auto const copyable_tool_text = ava::tui::detail::tool_card_copy_text(
      ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                 .name = "bash",
                                 .argument_summary = "command=git push origin main",
                                 .result_summary = "permission denied",
                                 .arguments_json = "{\"command\":\"git push origin main\"}",
                                 .result_json = "{\"tool\":\"bash\",\"exit_code\":126,\"stderr\":\"denied\\ntry again\"}",
                                 .lifecycle = ava::tui::ToolLifecycleState::Error,
                                 .permissions = {ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_push",
                                                                                   .resolver_request_id = "permission_1",
                                                                                   .decision = "deny",
                                                                                   .operation = "bash",
                                                                                   .tool_name = "bash",
                                                                                   .risk = "high",
                                                                                   .reason = "command can change external state",
                                                                                   .command = "git push origin main"}},
                                 .diff = "--- a\n+++ b\n-old\n+new",
                                 .changed_paths = {"src/main.cpp", "\x1b[31msecret.txt"},
                                 .truncated = true,
                                 .output_lines = 2,
                                 .total_lines = 10,
                                 .spill_path = "/tmp/ava-spill/bash.txt"});
  expect(copyable_tool_text.find("tool: bash") != std::string::npos && copyable_tool_text.find("command: git push origin main") != std::string::npos &&
             copyable_tool_text.find("shell status: exit 126") != std::string::npos && copyable_tool_text.find("permission: deny") != std::string::npos &&
             copyable_tool_text.find("risk high") != std::string::npos &&
             copyable_tool_text.find("reason command can change external state") != std::string::npos &&
             copyable_tool_text.find("inspect: /permissions audit show permreq_push") != std::string::npos &&
             copyable_tool_text.find("diagnose: /permissions diagnose permreq_push") != std::string::npos &&
             copyable_tool_text.find("output:\n  denied\n  try again") != std::string::npos &&
             copyable_tool_text.find("truncation: truncated 2/10 lines") != std::string::npos &&
             copyable_tool_text.find("changed: src/main.cpp, ?[31msecret.txt") != std::string::npos &&
             copyable_tool_text.find("spill: /tmp/ava-spill/bash.txt") != std::string::npos &&
             copyable_tool_text.find("diff:\n  --- a\n  +++ b") != std::string::npos && copyable_tool_text.find("\x1b[") == std::string::npos,
         "tui exposes plain copy text for detailed tool cards without ANSI styling");

  auto const permission_copy_text = ava::tui::detail::tool_card_permission_copy_text(
      ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                 .name = "bash",
                                 .permissions = {ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_push",
                                                                                   .resolver_request_id = "permission_1",
                                                                                   .decision = "deny",
                                                                                   .operation = "bash",
                                                                                   .tool_name = "bash",
                                                                                   .risk = "high",
                                                                                   .reason = "command can change external state",
                                                                                   .command = "git push origin main"},
                                                 ava::tui::ToolPermissionAuditItem{.permission_request_id = "permreq_read",
                                                                                   .decision = "allow",
                                                                                   .operation = "read",
                                                                                   .tool_name = "read_file",
                                                                                   .risk = "low",
                                                                                   .target = "docs/USAGE.md"}}},
      "git push");
  auto const missing_permission_copy_text = ava::tui::detail::tool_card_permission_copy_text(
      ava::tui::ToolTimelineItem{
          .status = ava::tui::ToolTimelineStatus::Success,
          .name = "read",
          .permissions = {ava::tui::ToolPermissionAuditItem{
              .permission_request_id = "permreq_read", .decision = "allow", .operation = "read", .tool_name = "read_file", .target = "docs/USAGE.md"}}},
      "git push");
  expect(permission_copy_text.find("permission: deny") != std::string::npos && permission_copy_text.find("id permreq_push") != std::string::npos &&
             permission_copy_text.find("risk high") != std::string::npos &&
             permission_copy_text.find("reason command can change external state") != std::string::npos &&
             permission_copy_text.find("command git push origin main") != std::string::npos &&
             permission_copy_text.find("inspect: /permissions audit show permreq_push") != std::string::npos &&
             permission_copy_text.find("diagnose: /permissions diagnose permreq_push") != std::string::npos &&
             permission_copy_text.find("permreq_read") == std::string::npos && permission_copy_text.find("\x1b[") == std::string::npos &&
             missing_permission_copy_text.empty(),
         "tui exposes focused permission copy text with follow-up commands, query filtering, and no ANSI styling");

  auto const copyable_diff_text = ava::tui::detail::tool_card_diff_copy_text(ava::tui::ToolTimelineItem{
      .status = ava::tui::ToolTimelineStatus::Success, .name = "write", .diff = "--- note.txt\n+++ note.txt\n-\x1b[31mold\n+new", .diff_truncated = true});
  expect(copyable_diff_text.find("--- note.txt") != std::string::npos && copyable_diff_text.find("-old") != std::string::npos &&
             copyable_diff_text.find("+new") != std::string::npos && copyable_diff_text.find("[diff truncated]") != std::string::npos &&
             copyable_diff_text.find("\x1b[") == std::string::npos,
         "tui exposes focused plain copy text for latest tool diffs without ANSI styling");

  auto const queryable_tool_card =
      ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                 .name = "write",
                                 .argument_summary = "src/main.cpp",
                                 .result_summary = "wrote 27 bytes",
                                 .permissions = {ava::tui::ToolPermissionAuditItem{
                                     .permission_request_id = "permreq_write", .decision = "allow", .tool_name = "write_file", .reason = "workspace edit"}},
                                 .diff = "--- src/main.cpp\n+++ src/main.cpp\n-old\n+new",
                                 .changed_paths = {"src/main.cpp"}};
  expect(ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "MAIN.CPP") &&
             ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "workspace edit") &&
             ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "+new") &&
             !ava::tui::detail::tool_card_matches_copy_query(queryable_tool_card, "package-lock"),
         "tui copy query matching finds visible tool, permission, changed-path, and diff context case-insensitively");

  auto const collapsed_override_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "bash",
                                                                                                            .argument_summary = "command=ctest",
                                                                                                            .result_summary = "ok",
                                                                                                            .details_visible = false}}},
                                 .width = 48,
                                 .height = 10,
                                 .tool_details_visible = true});
  expect(
      std::ranges::none_of(collapsed_override_card, [](std::string const& line) { return strip_sgr(line).find("args: command=ctest") != std::string::npos; }),
      "tui supports per-tool detail collapse even when the global details toggle is enabled");

  auto const expanded_override_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "grep",
                                                                                                            .argument_summary = "pattern=todo",
                                                                                                            .result_summary = "2 matches",
                                                                                                            .details_visible = true,
                                                                                                            .truncated = true,
                                                                                                            .visible_matches = 2,
                                                                                                            .total_matches = 10,
                                                                                                            .spill_path = "/tmp/ava-spill/grep.txt",
                                                                                                            .spill_truncated = true}}},
                                 .width = 72,
                                 .height = 12});
  expect(std::ranges::any_of(expanded_override_card,
                             [](std::string const& line) { return strip_sgr(line).find("truncation: truncated 2/10 matches") != std::string::npos; }) &&
             std::ranges::any_of(expanded_override_card,
                                 [](std::string const& line) { return strip_sgr(line).find("spill: /tmp/ava-spill/grep.txt") != std::string::npos; }),
         "tui renders backend-provided truncation counts and spill paths only when present");

  auto const changed_paths_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                             .name = "apply_patch",
                                             .argument_summary = "2 edits",
                                             .result_summary = "updated files",
                                             .details_visible = true,
                                             .changed_paths = {"src/main.cpp", "tests/tui.cpp", "docs/mvp.md", "goals/ledger.md", "\x1b[31mhidden.txt"}}}},
      .width = 120,
      .height = 16});
  expect(std::ranges::any_of(changed_paths_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("changed:") != std::string::npos && visible.find("src/main.cpp") != std::string::npos &&
                                      visible.find("+1 more") != std::string::npos && visible.find("\x1b[") == std::string::npos;
                             }) &&
             std::ranges::all_of(changed_paths_card, [](std::string const& line) { return visible_columns(line) <= 120; }),
         "tui expanded tool cards render bounded changed-file summaries even without a diff");

  auto const inferred_changed_tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                .name = "write_file",
                                                                .argument_summary = "path=notes/output.txt",
                                                                .result_summary = "wrote 12 bytes",
                                                                .result_json = "{\"tool\":\"write_file\",\"ok\":true,\"path\":\"notes/output.txt\"}",
                                                                .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                .details_visible = true,
                                                                .diff = "--- notes/output.txt\n+++ notes/output.txt\n-old\n+new"};
  auto const inferred_changed_card =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {ava::tui::TranscriptItem{.tool = inferred_changed_tool}},
                                                           .width = 88,
                                                           .height = 14});
  auto const inferred_changed_copy = ava::tui::detail::tool_card_copy_text(inferred_changed_tool);
  expect(std::ranges::any_of(inferred_changed_card,
                             [](std::string const& line) { return strip_sgr(line).find("changed: notes/output.txt") != std::string::npos; }) &&
             std::ranges::any_of(inferred_changed_card,
                                 [](std::string const& line) { return strip_sgr(line).find("diff notes/output.txt:") != std::string::npos; }) &&
             inferred_changed_copy.find("changed: notes/output.txt") != std::string::npos &&
             ava::tui::detail::tool_card_matches_copy_query(inferred_changed_tool, "output.txt"),
         "tui infers changed-file summaries from mutating tool result JSON when timeline metadata is sparse");

  auto const wide_diff_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                                            .name = "edit_file",
                                                                                                            .argument_summary = "path=note.txt",
                                                                                                            .result_summary = "wrote 9 bytes",
                                                                                                            .details_visible = true,
                                                                                                            .diff = "--- note.txt\n+++ note.txt\n-old\n+new",
                                                                                                            .diff_truncated = true}}},
                                 .width = 88,
                                 .height = 14});
  expect(std::ranges::any_of(wide_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
             std::ranges::any_of(wide_diff_card,
                                 [](std::string const& line) {
                                   return strip_sgr(line).find("+new") != std::string::npos && line.find("\x1b[38;2;52;211;153m") != std::string::npos;
                                 }) &&
             std::ranges::any_of(wide_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff truncated") != std::string::npos; }),
         "tui renders backend-provided unified diff previews with mutation colors and truncation markers");

  auto const narrow_diff_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "edit_file",
                                                                                 .argument_summary = "path=very/long/path/to/note.txt",
                                                                                 .result_summary = "wrote 9 bytes",
                                                                                 .details_visible = true,
                                                                                 .diff = "--- very/long/path/to/note.txt\n+++ very/long/path/to/"
                                                                                         "note.txt\n-old value\n+new value"}}},
      .width = 36,
      .height = 14});
  expect(std::ranges::any_of(narrow_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff:") != std::string::npos; }) &&
             std::ranges::all_of(narrow_diff_card, [](std::string const& line) { return visible_columns(line) <= 36; }),
         "tui keeps backend-provided diff previews width-safe on narrow terminals");

  auto const bash_ux_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Error,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=ctest --test-dir build",
                                                                                 .result_summary = "exit 1",
                                                                                 .arguments_json = "{\"command\":\"ctest --test-dir build\"}",
                                                                                 .result_json = "{\"tool\":\"bash\",\"exit_code\":1,"
                                                                                                "\"duration_ms\":1530,\"total_lines\":4,"
                                                                                                "\"output_lines\":4,\"output\":"
                                                                                                "\"configure\\nbuild\\nfail\\nsummary\"}"}}},
      .width = 72,
      .height = 16});
  expect(std::ranges::any_of(bash_ux_card,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("bash") != std::string::npos && visible.find("command=ctest --test-dir build") != std::string::npos;
                             }) &&
             std::ranges::any_of(bash_ux_card, [](std::string const& line) { return strip_sgr(line).find("exit 1 · 1.5s") != std::string::npos; }) &&
             std::ranges::any_of(bash_ux_card,
                                 [](std::string const& line) { return strip_sgr(line).find("output: 2 shown/4 lines · 2 hidden") != std::string::npos; }) &&
             std::ranges::any_of(bash_ux_card, [](std::string const& line) { return strip_sgr(line).find("configure") != std::string::npos; }) &&
             std::ranges::all_of(bash_ux_card, [](std::string const& line) { return visible_columns(line) <= 72; }),
         "tui renders bash cards with command, exit status, duration, and collapsed output hidden-line counts");

  auto const expanded_bash_ux_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=cmake --build build",
                                                                                 .result_summary = "exit 0",
                                                                                 .arguments_json = "{\"command\":\"cmake --build build\"}",
                                                                                 .result_json = "{\"tool\":\"bash\",\"exit_code\":0,"
                                                                                                "\"duration_ms\":250,\"total_lines\":3,"
                                                                                                "\"output_lines\":3,\"output\":"
                                                                                                "\"[1/2] compile\\n[2/2] link\\nok\"}"}}},
      .width = 80,
      .height = 18,
      .tool_details_visible = true});
  expect(std::ranges::any_of(expanded_bash_ux_card,
                             [](std::string const& line) { return strip_sgr(line).find("command: cmake --build build") != std::string::npos; }) &&
             std::ranges::any_of(expanded_bash_ux_card, [](std::string const& line) { return strip_sgr(line).find("status: exit 0") != std::string::npos; }) &&
             std::ranges::any_of(expanded_bash_ux_card, [](std::string const& line) { return strip_sgr(line).find("duration: 250ms") != std::string::npos; }) &&
             std::ranges::any_of(expanded_bash_ux_card,
                                 [](std::string const& line) { return strip_sgr(line).find("output: 3 shown lines") != std::string::npos; }),
         "tui expanded bash cards show command/status/duration and a wider output preview");

  auto const quoted_bash_summary_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=echo \"a, b\", timeout=1000",
                                                                                 .result_summary = "exit 0",
                                                                                 .details_visible = true}}},
      .width = 80,
      .height = 14});
  expect(std::ranges::any_of(quoted_bash_summary_card,
                             [](std::string const& line) { return strip_sgr(line).find("command: echo \"a, b\"") != std::string::npos; }) &&
             std::ranges::none_of(quoted_bash_summary_card,
                                  [](std::string const& line) {
                                    return strip_sgr(line).find("command: echo \"a") != std::string::npos && strip_sgr(line).find("b\"") == std::string::npos;
                                  }),
         "tui bash cards do not split fallback command summaries at comma-space inside shell quotes");

  auto const escaped_bash_summary_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "bash",
                                                                                 .argument_summary = "command=printf foo\\, bar, timeout=1000",
                                                                                 .result_summary = "exit 0",
                                                                                 .details_visible = true}}},
      .width = 80,
      .height = 14});
  expect(std::ranges::any_of(escaped_bash_summary_card,
                             [](std::string const& line) { return strip_sgr(line).find("command: printf foo\\, bar") != std::string::npos; }),
         "tui bash cards do not split fallback command summaries at escaped comma-space");

  auto const running_bash_ux_card = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Running,
                                                                                                            .name = "bash",
                                                                                                            .argument_summary = "command=long-test",
                                                                                                            .arguments_json = "{\"command\":\"long-test\"}"}}},
                                 .width = 52,
                                 .height = 10});
  expect(std::ranges::any_of(running_bash_ux_card,
                             [](std::string const& line) { return strip_sgr(line).find("running · Esc/Ctrl+C stop") != std::string::npos; }) &&
             std::ranges::all_of(running_bash_ux_card, [](std::string const& line) { return visible_columns(line) <= 52; }),
         "tui running bash cards include the existing active-run cancel hint without backend mutation");

  auto const rich_diff_card = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                 .name = "edit_file",
                                                                                 .argument_summary = "path=note.txt",
                                                                                 .result_summary = "edited note.txt",
                                                                                 .details_visible = true,
                                                                                 .diff = "--- note.txt\n+++ note.txt\n@@ -1,2 +1,2 @@\n-old\n+new\n context",
                                                                                 .changed_paths = {"note.txt"}}}},
      .width = 92,
      .height = 18});
  expect(std::ranges::any_of(rich_diff_card, [](std::string const& line) { return strip_sgr(line).find("diff note.txt:") != std::string::npos; }) &&
             std::ranges::any_of(rich_diff_card, [](std::string const& line) { return strip_sgr(line).find("hunk @@ -1,2 +1,2 @@") != std::string::npos; }) &&
             std::ranges::any_of(rich_diff_card,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("1") != std::string::npos && visible.find("-old") != std::string::npos;
                                 }) &&
             std::ranges::any_of(rich_diff_card,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("1") != std::string::npos && visible.find("+new") != std::string::npos;
                                 }),
         "tui diff cards render unified hunk boundaries with line-numbered added and removed rows");

  auto const sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "hello"}},
      .width = 128,
      .height = 22,
      .sidebar = ava::tui::SidebarSnapshot{.activity = {ava::tui::SidebarActivityItem{
                                               .id = "call_1", .label = "bash", .detail = "running tests", .status = ava::tui::ToolTimelineStatus::Running}},
                                           .modified_files = {ava::tui::SidebarModifiedFile{.path = "src/ava/tui/runtime.cpp", .added = 12, .removed = 3}},
                                           .session_id = "session_test\x1b[31m",
                                           .mode = "build\x1b[31m",
                                           .provider = "openai\x1b[31m",
                                           .model = "gpt-5.5\x1b[31m",
                                           .workspace = "/workspace/project\x1b[31m",
                                           .git_branch = "develop\x1b[31m",
                                           .version = "0.32",
                                           .token_status = "1.2k (4.0%)",
                                           .reasoning_status = "low\x1b[31m",
                                           .context_source_count = 2}});
  expect(std::ranges::any_of(sidebar_frame,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("Activity") != std::string::npos && visible.find("Modified Files") == std::string::npos;
                             }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("bash") != std::string::npos && visible.find("running tests") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("src/ava/tui/runtime.cpp") != std::string::npos && visible.find("+12") != std::string::npos &&
                                          visible.find("-3") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("branch develop") != std::string::npos || visible.find("AVA 0.32") != std::string::npos;
                                 }) &&
             std::ranges::any_of(sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("usage 1.2k (4.0%)") != std::string::npos; }) &&
             std::ranges::any_of(sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("reasoning low") != std::string::npos; }) &&
             std::ranges::any_of(sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("context sources 2") != std::string::npos; }) &&
             std::ranges::none_of(sidebar_frame, [](std::string const& line) { return line.find("\x1b[31m") != std::string::npos; }) &&
             std::ranges::all_of(sidebar_frame, [](std::string const& line) { return visible_columns(line) <= 128; }),
         "tui renders a sidebar with activity, modified files, session metadata, and version");
  expect(std::ranges::any_of(sidebar_frame,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               auto const activity = visible.find("Activity");
                               auto const separator = visible.find("│");
                               return activity != std::string::npos && separator != std::string::npos && separator < activity && activity >= 90;
                             }),
         "tui pads blank main rows so sidebar content stays in the right column");

  auto const idle_after_completed_activity_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 128,
      .height = 18,
      .sidebar = ava::tui::SidebarSnapshot{
          .activity = {ava::tui::SidebarActivityItem{
              .id = "responding", .label = "responding", .detail = "assistant responded", .status = ava::tui::ToolTimelineStatus::Success}}}});
  expect(std::ranges::any_of(idle_after_completed_activity_frame, [](std::string const& line) { return strip_sgr(line).find("idle") != std::string::npos; }) &&
             std::ranges::none_of(idle_after_completed_activity_frame,
                                  [](std::string const& line) { return strip_sgr(line).find("assistant responded") != std::string::npos; }),
         "tui sidebar treats completed assistant activity as idle instead of persistent history");

  auto const unknown_sidebar_frame = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {},
                                 .width = 120,
                                 .height = 18,
                                 .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test", .mode = "build", .provider = "openai"}});
  expect(
      std::ranges::any_of(unknown_sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("usage tokens unknown") != std::string::npos; }) &&
          std::ranges::any_of(unknown_sidebar_frame,
                              [](std::string const& line) { return strip_sgr(line).find("context sources unknown") != std::string::npos; }),
      "tui sidebar labels missing usage and context values as unknown instead of inventing numbers");

  auto const zero_context_sidebar_frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_test",
      .input = "",
      .status = "ready",
      .transcript = {},
      .width = 120,
      .height = 18,
      .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test", .mode = "build", .provider = "openai", .context_source_count = 0}});
  expect(
      std::ranges::any_of(zero_context_sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("context sources 0") != std::string::npos; }),
      "tui sidebar distinguishes a known zero context source count from unknown context data");

  auto const long_session_sidebar_frame =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = {},
                                                           .width = 132,
                                                           .height = 20,
                                                           .sidebar = ava::tui::SidebarSnapshot{.session_id = "session_test",
                                                                                                .mode = "build",
                                                                                                .provider = "openai",
                                                                                                .model = "gpt-5.5",
                                                                                                .workspace = "/workspace/project",
                                                                                                .git_branch = "develop",
                                                                                                .version = "0.32",
                                                                                                .token_status = "180k (90.0%)",
                                                                                                .reasoning_status = std::nullopt,
                                                                                                .context_source_count = 3,
                                                                                                .session_path = "/tmp/ava/sessions/session_test.jsonl",
                                                                                                .session_entry_count = 42}});
  expect(std::ranges::any_of(long_session_sidebar_frame,
                             [](std::string const& line) { return strip_sgr(line).find("path /tmp/ava/sessions") != std::string::npos; }) &&
             std::ranges::any_of(long_session_sidebar_frame, [](std::string const& line) { return strip_sgr(line).find("entries 42") != std::string::npos; }) &&
             std::ranges::any_of(long_session_sidebar_frame,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("context pressure critical 90.0%") != std::string::npos;
                                 }),
         "tui sidebar surfaces current session path, entry count, and context pressure when snapshot data exists");

  auto const narrow_no_sidebar = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {},
                                 .width = 90,
                                 .height = 10,
                                 .sidebar = ava::tui::SidebarSnapshot{.activity = {ava::tui::SidebarActivityItem{.id = "a", .label = "sidebar-only"}}}});
  expect(std::ranges::none_of(narrow_no_sidebar, [](std::string const& line) { return strip_sgr(line).find("sidebar-only") != std::string::npos; }),
         "tui hides the sidebar on narrow terminals");

  auto const tabbed = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                           .provider = "openai",
                                                                           .model = "gpt-5.5",
                                                                           .session_id = "session_test",
                                                                           .input = "",
                                                                           .status = "tab\tstatus",
                                                                           .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "tab\ttext"}},
                                                                           .width = 30,
                                                                           .height = 8});
  expect(std::ranges::none_of(tabbed, [](std::string const& line) { return line.find('\t') != std::string::npos; }) &&
             std::ranges::all_of(tabbed, [](std::string const& line) { return visible_columns(line) <= 30; }),
         "tui expands tabs before rendering width-bounded lines");

  auto const assistant_meta = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "answer", .meta = "Build · GPT-5.5 · 1.2s"}},
                                 .width = 48,
                                 .height = 10});
  expect(std::ranges::any_of(assistant_meta, [](std::string const& line) { return strip_sgr(line).find("* Build · GPT-5.5 · 1.2s") != std::string::npos; }) &&
             std::ranges::all_of(assistant_meta, [](std::string const& line) { return visible_columns(line) <= 48; }),
         "tui renders assistant mode/model/duration metadata under AVA messages with ASCII markers");
  auto assistant_meta_index = std::optional<std::size_t>{};
  auto composer_index = std::optional<std::size_t>{};
  for (std::size_t index = 0; index < assistant_meta.size(); ++index)
  {
    auto const visible = strip_sgr(assistant_meta[index]);
    if (!assistant_meta_index && visible.find("* Build · GPT-5.5 · 1.2s") != std::string::npos)
    {
      assistant_meta_index = index;
    }
    if (!composer_index && visible.find("│  Type a message...") != std::string::npos)
      composer_index = index;
  }
  expect(assistant_meta_index && composer_index && *composer_index > *assistant_meta_index + 1 && strip_sgr(assistant_meta[*assistant_meta_index + 1]).empty(),
         "tui leaves a vertical margin between the latest assistant metadata and the composer");

  std::string exact_width_utf8_status;
  for (int index = 0; index < 12; ++index)
  {
    exact_width_utf8_status += "\xC3\xA9";
  }
  auto const exact_width_utf8 = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                     .provider = "openai",
                                                                                     .model = "gpt-5.5",
                                                                                     .session_id = "session_test",
                                                                                     .input = "",
                                                                                     .status = exact_width_utf8_status,
                                                                                     .transcript = {},
                                                                                     .width = 20,
                                                                                     .height = 8});
  expect(std::ranges::all_of(exact_width_utf8, [](std::string const& line) { return visible_columns(line) <= 20; }) &&
             std::ranges::any_of(exact_width_utf8, [](std::string const& line) { return strip_sgr(line).find("│  GPT-5.5") != std::string::npos; }),
         "tui width fitting preserves the AVA composer surface at minimum width");

  auto const utf8 = ava::tui::render_composer(
      ava::tui::ComposerSnapshot{.mode = "build",
                                 .provider = "openai",
                                 .model = "gpt-5.5",
                                 .session_id = "session_test",
                                 .input = "",
                                 .status = "ready",
                                 .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = std::string(13, 'x') + "\xC3\xA9" + "zzz"}},
                                 .width = 20,
                                 .height = 8});
  expect(std::ranges::none_of(utf8, [](std::string const& line) { return !line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0U) == 0xC0U; }),
         "tui truncation does not leave a trailing utf-8 starter byte");

  std::vector<ava::tui::TranscriptItem> stress_transcript;
  for (int index = 0; index < 36; ++index)
  {
    stress_transcript.push_back(ava::tui::TranscriptItem{
        .label = "you", .text = "resize stress user line " + std::to_string(index) + " with a very-long-token-that-must-not-overflow-or-resize-the-layout"});
    stress_transcript.push_back(ava::tui::TranscriptItem{.label = "ava",
                                                         .text = "assistant answer " + std::to_string(index) +
                                                                 " keeps CJK \xE7\x95\x8C and emoji \xF0\x9F\x98\x80 "
                                                                 "inside the measured viewport",
                                                         .meta = "Build · GPT-5.5",
                                                         .thinking = "checked resize path " + std::to_string(index)});
    if (index % 5 == 0)
    {
      stress_transcript.push_back(ava::tui::TranscriptItem{
          .tool = ava::tui::ToolTimelineItem{.status = index % 10 == 0 ? ava::tui::ToolTimelineStatus::Error : ava::tui::ToolTimelineStatus::Success,
                                             .name = "grep",
                                             .argument_summary = "pattern=needle path=src",
                                             .result_summary = "returned " + std::to_string(index) + " matches",
                                             .call_id = "call_resize_" + std::to_string(index),
                                             .lifecycle = index % 10 == 0 ? ava::tui::ToolLifecycleState::Error : ava::tui::ToolLifecycleState::Complete,
                                             .truncated = true,
                                             .visible_matches = 2,
                                             .total_matches = 12,
                                             .spill_path = "/tmp/ava-spill/resize.txt"}});
    }
    if (index % 7 == 0)
    {
      stress_transcript.push_back(ava::tui::TranscriptItem{.label = "audit", .text = "permission replied after resize boundary " + std::to_string(index)});
    }
  }

  ava::tui::SidebarSnapshot const stress_sidebar{
      .activity =
          {ava::tui::SidebarActivityItem{
               .id = "running", .label = "compaction", .detail = "compaction started tokens~9000/8000", .status = ava::tui::ToolTimelineStatus::Running},
           ava::tui::SidebarActivityItem{.id = "done", .label = "read_file", .detail = "assistant responded", .status = ava::tui::ToolTimelineStatus::Success}},
      .modified_files = {ava::tui::SidebarModifiedFile{.path = "src/ava/tui/composer.cpp", .added = 3, .removed = 1}},
      .session_id = "session_resize_stress",
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .workspace = "/workspace",
      .git_branch = "develop",
      .version = "test",
      .token_status = "tokens unknown",
      .context_source_count = 2};
  std::vector<std::size_t> const stress_widths = {1, 20, 28, 40, 72, 111, 112, 160};
  std::vector<std::size_t> const stress_heights = {1, 8, 10, 18, 32};
  for (auto const width : stress_widths)
  {
    for (auto const height : stress_heights)
    {
      auto frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                        .provider = "openai",
                                                                        .model = "gpt-5.5",
                                                                        .session_id = "session_resize_stress",
                                                                        .input = "draft line one\nsecond draft line with \xE7\x95\x8C",
                                                                        .status = "ready",
                                                                        .processing = true,
                                                                        .token_status = "tokens unknown",
                                                                        .reasoning_status = "thinking visible",
                                                                        .transcript = stress_transcript,
                                                                        .transcript_scroll_offset = 50,
                                                                        .width = width,
                                                                        .height = height,
                                                                        .input_cursor = std::string::npos,
                                                                        .sidebar = stress_sidebar,
                                                                        .tool_details_visible = true,
                                                                        .thinking_visible = true});
      auto const effective_width = std::max<std::size_t>(ava::tui::detail::kMinWidth, width);
      auto const effective_height = std::max<std::size_t>(ava::tui::detail::kMinHeight, height);
      expect(frame.size() == effective_height &&
                 std::ranges::all_of(frame,
                                     [&](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= effective_width; }),
             "tui resize stress render keeps long mixed transcripts bounded at every tested viewport");
    }
  }
}

void test_tui_session_grant_registry()
{
  auto make_prompt = [] {
    ava::permissions::CommandPermissionMetadata metadata;
    metadata.level = ava::command::CommandLevel::Standard;
    metadata.executor_identity_verified = true;
    metadata.containment_available = true;
    metadata.containment_status = ava::permissions::CommandContainmentStatus::Available;
    metadata.backend_maximum_scope = ava::command::InteractiveScope::Session;
    metadata.global_recipe_key = "global-recipe";
    metadata.workspace_recipe_key = "workspace-recipe";
    metadata.effective_allowed_scopes = {ava::command::InteractiveScope::Once, ava::command::InteractiveScope::Session};
    return ava::permissions::PermissionPrompt{.permission_request_id = "permreq_tui_session_grant",
                                              .operation = ava::permissions::Operation::RunCommand,
                                              .mode = ava::agent::Mode::Build,
                                              .workspace_dir = {},
                                              .target_path = {},
                                              .command = "ctest --test-dir build",
                                              .tool_name = "bash",
                                              .reason = "sealed workspace recipe",
                                              .risk = ava::permissions::PermissionRisk::Medium,
                                              .command_metadata = std::move(metadata)};
  };

  auto prompt = make_prompt();
  ava::tui::TuiSessionGrantRegistry registry;
  auto const created = registry.add("session_a", prompt);
  auto const reused = registry.add("session_a", prompt);
  expect(ava::tui::tui_session_grant_eligible(prompt) && created == ava::tui::TuiSessionGrantInsertResult::Added &&
             reused == ava::tui::TuiSessionGrantInsertResult::AlreadyPresent && registry.size() == 1 && registry.matches("session_a", prompt),
         "TUI session grants create once and reuse the exact eligible command recipe");

  auto different_mode = prompt;
  different_mode.mode = ava::agent::Mode::Plan;
  auto different_tool = prompt;
  different_tool.tool_name = "shell";
  auto different_recipe = prompt;
  different_recipe.command_metadata->workspace_recipe_key = "another-workspace-recipe";
  expect(!registry.matches("session_b", prompt) && !registry.matches("session_a", different_mode) && !registry.matches("session_a", different_tool) &&
             !registry.matches("session_a", different_recipe),
         "TUI session grants require exact session, mode, tool, and workspace recipe matches");

  auto no_longer_eligible = prompt;
  no_longer_eligible.command_metadata->effective_allowed_scopes = {ava::command::InteractiveScope::Once};
  expect(!ava::tui::tui_session_grant_eligible(no_longer_eligible) && !registry.matches("session_a", no_longer_eligible) &&
             registry.add("session_a", no_longer_eligible) == ava::tui::TuiSessionGrantInsertResult::Ineligible,
         "TUI session grants recheck backend eligibility before reuse or creation");

  ava::tui::TuiSessionGrantRegistry capped_registry;
  bool filled_to_cap = true;
  for (std::size_t index = 0; index < ava::tui::TuiSessionGrantRegistry::kMaxGrants; ++index)
  {
    auto distinct_prompt = make_prompt();
    distinct_prompt.command_metadata->workspace_recipe_key = "workspace-recipe-" + std::to_string(index);
    filled_to_cap = filled_to_cap && capped_registry.add("session_a", distinct_prompt) == ava::tui::TuiSessionGrantInsertResult::Added;
  }
  auto overflow_prompt = make_prompt();
  overflow_prompt.command_metadata->workspace_recipe_key = "workspace-recipe-overflow";
  expect(filled_to_cap && capped_registry.size() == ava::tui::TuiSessionGrantRegistry::kMaxGrants &&
             capped_registry.add("session_a", overflow_prompt) == ava::tui::TuiSessionGrantInsertResult::Full,
         "TUI session grants fail closed at the 64-grant in-memory cap");

  ava::tui::TuiSessionGrantRegistry same_session_registry;
  static_cast<void>(same_session_registry.add("session_a", prompt));
  expect(!same_session_registry.clear_for_session_transition("session_a", "session_a") && same_session_registry.matches("session_a", prompt),
         "applying an unchanged runtime session id preserves grants created in this TUI process");

  struct SessionTransition
  {
    std::string command;
    std::string session_id;
  };
  std::vector<SessionTransition> const transitions = {{"/new", "session_new"},
                                                      {"/resume session_resume", "session_resume"},
                                                      {"/fork", "session_fork"},
                                                      {"/clone", "session_clone"},
                                                      {"/import --confirm", "session_import"}};
  bool all_submit_transitions_clear = true;
  for (auto const& transition : transitions)
  {
    ava::tui::TuiSessionGrantRegistry transition_registry;
    static_cast<void>(transition_registry.add("session_a", prompt));
    ava::tui::TuiRuntimeStateSnapshot post_submit_state;
    post_submit_state.session_id = transition.session_id;
    ava::tui::TuiSubmitResult completed_submit;
    completed_submit.state_snapshot = std::move(post_submit_state);
    bool const cleared =
        completed_submit.state_snapshot && transition_registry.clear_for_session_transition("session_a", completed_submit.state_snapshot->session_id);
    all_submit_transitions_clear =
        all_submit_transitions_clear && cleared && transition_registry.size() == 0 && !transition_registry.matches(transition.session_id, prompt);
  }
  expect(all_submit_transitions_clear,
         "post-submit runtime snapshots clear TUI session grants for /new, /resume, /fork, /clone, and confirmed import transitions");

  auto const session_only_permission_modal =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_a",
                                                           .input = "",
                                                           .status = "permission required",
                                                           .transcript = {},
                                                           .permission_prompt = ava::tui::PermissionPromptView{.tool_name = "bash",
                                                                                                               .operation = "bash",
                                                                                                               .target = "",
                                                                                                               .command = "ctest --test-dir build",
                                                                                                               .reason = "",
                                                                                                               .risk = "medium",
                                                                                                               .allow_session_available = true},
                                                           .width = 120,
                                                           .height = 10});
  expect(
      std::ranges::any_of(session_only_permission_modal,
                          [](std::string const& line) { return strip_sgr(line).find("S allow session") != std::string::npos; }) &&
          std::ranges::none_of(session_only_permission_modal, [](std::string const& line) { return strip_sgr(line).find("R remember") != std::string::npos; }),
      "TUI permission key help does not advertise R when only an in-memory session grant is available");
}

void test_tui_text_model_conversions()
{
  auto const plain = ava::tui::text_from_plain("first\r\nsecond\nthird\rfour");
  expect(ava::tui::to_plain_text(plain) == "first\nsecond\nthird\nfour" && ava::tui::validate_text(plain).has_value(),
         "tui Text plain conversion normalizes CR/LF boundaries into explicit newline runs");

  auto appended = ava::tui::Text{};
  auto ok_string = ava::tui::append_string(appended, "plain");
  ava::tui::append_newline(appended);
  auto ok_span = ava::tui::append_span(appended, "code", ava::tui::Rendition{.code = true});
  auto bad_string = ava::tui::append_string(appended, "bad\nrun");
  auto bad_span = ava::tui::append_span(appended, "bad\rrun", ava::tui::Rendition{.bold = true});
  expect(ok_string.has_value() && ok_span.has_value() && !bad_string.has_value() && !bad_span.has_value() && ava::tui::to_plain_text(appended) == "plain\ncode",
         "tui Text builders reject embedded newlines inside string and span runs");

  auto invalid = ava::tui::Text{};
  invalid.runs.push_back(ava::tui::String{.text = "broken\nrun"});
  expect(!ava::tui::validate_text(invalid).has_value(), "tui Text validation catches invalid hand-built string runs");

  auto const markdown = ava::tui::text_from_markdown(
      "# Title\nSee [docs](https://example.test) and *note*.\nUse `ava` and **bold**.\n```cpp\nint main() "
      "{}\n```\nDrop ~~old~~ but keep ~literal~.\n**open");
  bool saw_code = false;
  bool saw_bold = false;
  bool saw_italic = false;
  bool saw_link = false;
  bool saw_strikethrough = false;
  for (auto const& run : markdown.runs)
  {
    if (auto const* span = std::get_if<ava::tui::TextSpan>(&run))
    {
      saw_code = saw_code || span->rendition.code;
      saw_bold = saw_bold || span->rendition.bold;
      saw_italic = saw_italic || span->rendition.italic;
      saw_link = saw_link || (span->rendition.underline && span->rendition.color == ava::tui::TextColorRole::Accent);
      saw_strikethrough = saw_strikethrough || span->rendition.strikethrough;
    }
  }
  auto const markdown_plain = ava::tui::to_plain_text(markdown);
  expect(ava::tui::validate_text(markdown).has_value() && saw_code && saw_bold && saw_italic && saw_link && saw_strikethrough &&
             markdown_plain.find("Title") != std::string::npos && markdown_plain.find("docs (https://example.test)") != std::string::npos &&
             markdown_plain.find("Use ava and bold.") != std::string::npos && markdown_plain.find("Drop old but keep ~literal~.") != std::string::npos &&
             markdown_plain.find("~~old~~") == std::string::npos && markdown_plain.find("**open") != std::string::npos,
         "tui Markdown conversion supports basic heading/link/emphasis/code/fences and leaves unsupported Markdown readable");

  auto const rendered_from_model = ava::tui::render_composer(ava::tui::ComposerSnapshot{
      .mode = "build",
      .provider = "openai",
      .model = "gpt-5.5",
      .session_id = "session_text_model",
      .input = "",
      .status = "ready",
      .transcript = {ava::tui::TranscriptItem{.label = "you", .text = "raw-user-hidden", .text_model = ava::tui::text_from_plain("model user visible")},
                     ava::tui::TranscriptItem{.label = "ava", .text = "", .text_model = ava::tui::text_from_plain("model assistant visible")},
                     ava::tui::TranscriptItem{.label = "ava",
                                              .text = "answer",
                                              .thinking = "raw-thinking-hidden",
                                              .thinking_model = ava::tui::text_from_plain("model thinking visible")},
                     ava::tui::TranscriptItem{.label = "error", .text = "raw-error-hidden", .text_model = ava::tui::text_from_plain("model error visible")}},
      .width = 80,
      .height = 22});
  std::string visible_model_text;
  for (auto const& line : rendered_from_model)
  {
    visible_model_text += strip_sgr(line);
    visible_model_text += '\n';
  }
  expect(visible_model_text.find("model user visible") != std::string::npos && visible_model_text.find("model assistant visible") != std::string::npos &&
             visible_model_text.find("model thinking visible") != std::string::npos && visible_model_text.find("model error visible") != std::string::npos &&
             visible_model_text.find("raw-user-hidden") == std::string::npos && visible_model_text.find("raw-thinking-hidden") == std::string::npos &&
             visible_model_text.find("raw-error-hidden") == std::string::npos,
         "tui transcript renderer consumes Text models for plain transcript, thinking, and fallback assistant paths");
}

void test_tui_event_state_reduces_runtime_events()
{
  ava::tui::TuiEventState state;

  ava::app::runtime::Event user;
  user.type = ava::app::runtime::EventType::UserMessage;
  user.text = "hello";
  ava::tui::apply_runtime_event(state, user);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Running && state.transcript.size() == 1 && state.transcript[0].label == "you" &&
             state.transcript[0].text == "hello" && ava::tui::to_plain_text(state.transcript[0].text_model) == "hello",
         "tui event state records user messages as completed transcript items");

  ava::app::runtime::Event delta;
  delta.type = ava::app::runtime::EventType::MessageUpdate;
  delta.model_id = "gpt-5.5";
  delta.text = "hel";
  ava::tui::apply_runtime_event(state, delta);
  delta.text = "lo";
  ava::tui::apply_runtime_event(state, delta);
  auto streaming_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_assistant_text == "hello" && streaming_snapshot.size() == 2 && streaming_snapshot[1].label == "ava" &&
             streaming_snapshot[1].text == "hello" && streaming_snapshot[1].meta == "Build · GPT-5.5" &&
             ava::tui::to_plain_text(streaming_snapshot[1].text_model) == "hello",
         "tui event state exposes pending assistant deltas and mode/model metadata in snapshots");

  ava::app::runtime::Event end;
  end.type = ava::app::runtime::EventType::MessageEnd;
  ava::tui::apply_runtime_event(state, end);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Completed && state.pending_assistant_text.empty() && state.transcript.size() == 2 &&
             state.transcript[1].label == "ava" && state.transcript[1].text == "hello" && state.transcript[1].meta == "Build · GPT-5.5" &&
             ava::tui::to_plain_text(state.transcript[1].text_model) == "hello",
         "tui event state commits assistant deltas on message end");
  expect(!state.activity.empty() && state.activity.back().id == "responding" && state.activity.back().status == ava::tui::ToolTimelineStatus::Success &&
             state.activity.back().detail == "assistant responded",
         "tui event state settles responding activity when assistant streaming ends");

  ava::tui::TuiEventState non_gpt_state;
  ava::app::runtime::Event non_gpt_delta;
  non_gpt_delta.type = ava::app::runtime::EventType::MessageUpdate;
  non_gpt_delta.provider_id = "anthropic";
  non_gpt_delta.model_id = "claude-sonnet-4-5";
  non_gpt_delta.text = "hi";
  ava::tui::apply_runtime_event(non_gpt_state, non_gpt_delta);
  auto const non_gpt_snapshot = ava::tui::event_state_transcript_snapshot(non_gpt_state);
  expect(non_gpt_snapshot.size() == 1 && non_gpt_snapshot[0].meta == "Build · Claude Sonnet 4.5",
         "tui event state uses centralized model profile display labels for non-GPT assistant metadata");

  ava::app::runtime::Event assistant_final;
  assistant_final.type = ava::app::runtime::EventType::AssistantMessage;
  assistant_final.text = "hello";
  ava::tui::apply_runtime_event(state, assistant_final);
  expect(state.transcript.size() == 2 && state.transcript[1].text == "hello", "tui event state avoids duplicating matching streamed assistant final events");

  assistant_final.text = "hello\n";
  ava::tui::apply_runtime_event(state, assistant_final);
  expect(state.transcript.size() == 2 && state.transcript[1].text == "hello",
         "tui event state treats trailing whitespace-only final changes as duplicate streamed assistant events");

  ava::tui::TuiEventState reasoning_state;
  ava::app::runtime::Event reasoning_start;
  reasoning_start.type = ava::app::runtime::EventType::ReasoningStart;
  reasoning_start.reasoning_format = "summary";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_start);
  ava::app::runtime::Event reasoning_delta;
  reasoning_delta.type = ava::app::runtime::EventType::ReasoningDelta;
  reasoning_delta.text = "checking";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_delta);
  reasoning_delta.text = " options";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_delta);
  auto reasoning_snapshot = ava::tui::event_state_transcript_snapshot(reasoning_state);
  expect(reasoning_state.pending_reasoning_text == "checking options" && reasoning_snapshot.size() == 1 && reasoning_snapshot[0].label == "ava" &&
             reasoning_snapshot[0].thinking == "checking options" && reasoning_snapshot[0].text.empty() &&
             ava::tui::to_plain_text(reasoning_snapshot[0].thinking_model) == "checking options",
         "tui event state exposes pending reasoning as part of the assistant turn");
  ava::app::runtime::Event reasoning_end;
  reasoning_end.type = ava::app::runtime::EventType::ReasoningEnd;
  ava::tui::apply_runtime_event(reasoning_state, reasoning_end);
  expect(reasoning_state.pending_reasoning_text == "checking options" && reasoning_state.transcript.empty() && reasoning_state.activity.size() == 1 &&
             reasoning_state.activity[0].label == "reasoning" && reasoning_state.activity[0].status == ava::tui::ToolTimelineStatus::Success,
         "tui event state keeps completed reasoning attached to the pending assistant turn");

  ava::app::runtime::Event reasoning_answer;
  reasoning_answer.type = ava::app::runtime::EventType::MessageUpdate;
  reasoning_answer.model_id = "gpt-5.5";
  reasoning_answer.text = "answer";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_answer);
  ava::app::runtime::Event reasoning_answer_end;
  reasoning_answer_end.type = ava::app::runtime::EventType::MessageEnd;
  reasoning_answer_end.model_id = "gpt-5.5";
  ava::tui::apply_runtime_event(reasoning_state, reasoning_answer_end);
  expect(reasoning_state.pending_reasoning_text.empty() && reasoning_state.transcript.size() == 1 && reasoning_state.transcript[0].label == "ava" &&
             reasoning_state.transcript[0].text == "answer" && reasoning_state.transcript[0].thinking == "checking options" &&
             ava::tui::to_plain_text(reasoning_state.transcript[0].text_model) == "answer" &&
             ava::tui::to_plain_text(reasoning_state.transcript[0].thinking_model) == "checking options",
         "tui event state commits reasoning and answer as one assistant transcript item");

  auto const thinking_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "",
                                                                                    .status = "ready",
                                                                                    .transcript = reasoning_state.transcript,
                                                                                    .width = 60,
                                                                                    .height = 10});
  expect(std::ranges::any_of(thinking_render, [](std::string const& line) { return strip_sgr(line).find("Thinking: checking options") != std::string::npos; }),
         "tui renders reasoning content as an inline thinking transcript block with a stable prefix");
  expect(std::ranges::any_of(thinking_render,
                             [](std::string const& line) {
                               return strip_sgr(line).find("Thinking:") != std::string::npos && line.find("\x1b[38;2;88;96;112m") != std::string::npos;
                             }),
         "tui renders thinking text with dim grey styling");
  expect(std::ranges::none_of(thinking_render,
                              [](std::string const& line) {
                                auto const visible = strip_sgr(line);
                                return visible.find("╭─ AVA") != std::string::npos || visible.find("AVA:") != std::string::npos ||
                                       visible.find("╭─ You") != std::string::npos || visible.find("You:") != std::string::npos;
                              }),
         "tui transcript role headers stay hidden for compact chat rendering");
  expect(std::ranges::none_of(thinking_render, [](std::string const& line) { return strip_sgr(line).find("╭─ Thinking") != std::string::npos; }),
         "tui thinking transcript block avoids the normal boxed message header");
  auto const hidden_thinking_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_test",
                                                                                           .input = "",
                                                                                           .status = "ready",
                                                                                           .transcript = reasoning_state.transcript,
                                                                                           .width = 60,
                                                                                           .height = 10,
                                                                                           .thinking_visible = false});
  expect(std::ranges::none_of(hidden_thinking_render,
                              [](std::string const& line) { return strip_sgr(line).find("Thinking: checking options") != std::string::npos; }) &&
             std::ranges::any_of(hidden_thinking_render, [](std::string const& line) { return strip_sgr(line).find("answer") != std::string::npos; }),
         "tui thinking visibility hides inline thinking blocks without hiding assistant text");

  ava::tui::TuiEventState redacted_reasoning_state;
  ava::app::runtime::Event redacted_reasoning;
  redacted_reasoning.type = ava::app::runtime::EventType::ReasoningDelta;
  redacted_reasoning.reasoning_redacted = true;
  redacted_reasoning.text = "provider-private-secret";
  ava::tui::apply_runtime_event(redacted_reasoning_state, redacted_reasoning);
  auto redacted_snapshot = ava::tui::event_state_transcript_snapshot(redacted_reasoning_state);
  auto const redacted_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                    .provider = "openai",
                                                                                    .model = "gpt-5.5",
                                                                                    .session_id = "session_test",
                                                                                    .input = "",
                                                                                    .status = "ready",
                                                                                    .transcript = redacted_snapshot,
                                                                                    .width = 60,
                                                                                    .height = 10});
  expect(
      redacted_snapshot.size() == 1 && redacted_snapshot[0].thinking == "[reasoning redacted]" &&
          std::ranges::none_of(redacted_render, [](std::string const& line) { return strip_sgr(line).find("provider-private-secret") != std::string::npos; }),
      "tui event state never renders text from redacted reasoning deltas");

  ava::tui::TuiEventState audit_state;
  ava::app::runtime::Event permission_audit;
  permission_audit.type = ava::app::runtime::EventType::ProviderEvent;
  permission_audit.status = "tui:permission_request";
  permission_audit.text = "permission requested: bash pwd";
  ava::tui::apply_runtime_event(audit_state, permission_audit);
  ava::app::runtime::Event question_audit;
  question_audit.type = ava::app::runtime::EventType::ProviderEvent;
  question_audit.status = "tui:question_answer";
  question_audit.text = "question answered: yes";
  ava::tui::apply_runtime_event(audit_state, question_audit);
  expect(audit_state.transcript.size() == 2 && audit_state.transcript[0].label == "audit" &&
             audit_state.transcript[0].text == "permission requested: bash pwd" && audit_state.transcript[1].text == "question answered: yes" &&
             !audit_state.activity.empty(),
         "tui event state records permission and question audit markers from resolver events");

  ava::tui::TuiEventState reused_state;
  ava::tui::apply_runtime_event(reused_state, user);
  delta.text = "streamed";
  ava::tui::apply_runtime_event(reused_state, delta);
  ava::tui::apply_runtime_event(reused_state, end);
  ava::app::runtime::Event next_user;
  next_user.type = ava::app::runtime::EventType::UserMessage;
  next_user.text = "next";
  ava::tui::apply_runtime_event(reused_state, next_user);
  assistant_final.text = "fresh final";
  ava::tui::apply_runtime_event(reused_state, assistant_final);
  expect(reused_state.transcript.size() == 4 && reused_state.transcript[1].text == "streamed" && reused_state.transcript.back().text == "fresh final",
         "tui event state clears streaming index before a reused-state next turn");

  ava::tui::TuiEventState final_state;
  assistant_final.text = "direct final";
  ava::tui::apply_runtime_event(final_state, assistant_final);
  expect(final_state.run_status == ava::tui::TuiEventRunStatus::Completed && final_state.transcript.size() == 1 && final_state.transcript[0].label == "ava" &&
             final_state.transcript[0].text == "direct final",
         "tui event state records assistant final events without streaming deltas");
  assistant_final.text = "Use `ava` and **bold**";
  ava::tui::apply_runtime_event(final_state, assistant_final);
  expect(final_state.transcript.size() == 2 && ava::tui::to_plain_text(final_state.transcript.back().text_model) == "Use ava and bold",
         "tui event state stores assistant Markdown as frontend-owned semantic Text");

  ava::tui::TuiEventState provider_state;
  ava::app::runtime::Event provider_start;
  provider_start.type = ava::app::runtime::EventType::ProviderEvent;
  provider_start.status = "tool_call_start";
  provider_start.call_id = "provider_call_1";
  provider_start.tool_name = "read_file";
  provider_start.text = R"({"path": "README.md"})";
  ava::tui::apply_runtime_event(provider_state, provider_start);
  auto provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  auto const provider_activity_id = provider_state.activity.empty() ? std::string{} : provider_state.activity[0].id;
  expect(provider_state.activity.size() == 1 && !provider_activity_id.empty() && provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "provider is preparing tool call" &&
             provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Running && provider_state.transcript.empty() &&
             provider_state.pending_tools.size() == 1 && provider_snapshot.size() == 1 && provider_snapshot.back().tool &&
             provider_snapshot.back().tool->lifecycle == ava::tui::ToolLifecycleState::ProviderAnnounced,
         "tui event state shows provider tool-call starts as pending announced tool cards");

  ava::app::runtime::Event provider_delta = provider_start;
  provider_delta.status = "tool_call_delta";
  provider_delta.tool_name.clear();
  provider_delta.text = R"({"path": "README.md", "partial": true})";
  ava::tui::apply_runtime_event(provider_state, provider_delta);
  provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  expect(provider_state.activity.size() == 1 && provider_state.activity[0].id == provider_activity_id && provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "streaming tool arguments" && provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Running &&
             provider_state.transcript.empty() && provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsStreaming &&
             provider_state.pending_tools[0].item.argument_summary.find("\"partial\": true") != std::string::npos && provider_snapshot.size() == 1 &&
             provider_snapshot.back().tool,
         "tui event state keeps provider tool-call deltas on the pending tool card and preserves labels by call id");

  ava::app::runtime::Event provider_end = provider_delta;
  provider_end.status = "tool_call_end";
  provider_end.text = R"({"path": "README.md", "complete": true})";
  ava::tui::apply_runtime_event(provider_state, provider_end);
  provider_snapshot = ava::tui::event_state_transcript_snapshot(provider_state);
  expect(provider_state.activity.size() == 1 && provider_state.activity[0].id == provider_activity_id && provider_state.activity[0].label == "read_file" &&
             provider_state.activity[0].detail == "tool call ready" && provider_state.activity[0].status == ava::tui::ToolTimelineStatus::Success &&
             provider_state.transcript.empty() && provider_state.pending_tools.size() == 1 &&
             provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsComplete && provider_snapshot.size() == 1 &&
             provider_snapshot.back().tool,
         "tui event state marks provider tool-call arguments complete without settling completed transcript history");

  ava::app::runtime::Event provider_execution_start;
  provider_execution_start.type = ava::app::runtime::EventType::ToolStart;
  provider_execution_start.call_id = "provider_call_1";
  provider_execution_start.tool_name = "read_file";
  provider_execution_start.text = "path=README.md";
  ava::tui::apply_runtime_event(provider_state, provider_execution_start);
  expect(provider_state.pending_tools.size() == 1 && provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ExecutionStarted &&
             provider_state.pending_tools[0].item.argument_summary == "path=README.md",
         "tui event state advances an announced provider tool card into execution by call id");

  ava::app::runtime::Event provider_execution_progress;
  provider_execution_progress.type = ava::app::runtime::EventType::ToolProgress;
  provider_execution_progress.call_id = "provider_call_1";
  provider_execution_progress.tool_name = "read_file";
  provider_execution_progress.text = "reading file";
  ava::tui::apply_runtime_event(provider_state, provider_execution_progress);
  expect(provider_state.pending_tools.size() == 1 && provider_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::Progress &&
             provider_state.pending_tools[0].item.result_summary == "reading file",
         "tui event state records partial tool progress on the pending card");

  ava::app::runtime::Event provider_execution_result;
  provider_execution_result.type = ava::app::runtime::EventType::ToolResult;
  provider_execution_result.call_id = "provider_call_1";
  provider_execution_result.tool_name = "read_file";
  provider_execution_result.status = "success";
  provider_execution_result.text = "read lines 1-10/10";
  ava::tui::apply_runtime_event(provider_state, provider_execution_result);
  expect(provider_state.pending_tools.empty() && !provider_state.transcript.empty() && provider_state.transcript.back().tool &&
             provider_state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Complete &&
             provider_state.transcript.back().tool->argument_summary == "path=README.md",
         "tui event state settles completed tools into immutable transcript history");

  ava::tui::TuiEventState provider_without_id_state;
  ava::app::runtime::Event provider_without_id;
  provider_without_id.type = ava::app::runtime::EventType::ProviderEvent;
  provider_without_id.status = "tool_call_start";
  provider_without_id.tool_name = "grep";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  auto const provider_without_id_activity_id = provider_without_id_state.activity.empty() ? std::string{} : provider_without_id_state.activity[0].id;
  provider_without_id.status = "tool_call_delta";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  provider_without_id.status = "tool_call_end";
  ava::tui::apply_runtime_event(provider_without_id_state, provider_without_id);
  expect(provider_without_id_state.activity.size() == 1 && !provider_without_id_activity_id.empty() &&
             provider_without_id_state.activity[0].id == provider_without_id_activity_id && provider_without_id_state.activity[0].label == "grep" &&
             provider_without_id_state.activity[0].detail == "tool call ready" &&
             provider_without_id_state.activity[0].status == ava::tui::ToolTimelineStatus::Success && provider_without_id_state.pending_tools.size() == 1 &&
             provider_without_id_state.pending_tools[0].item.lifecycle == ava::tui::ToolLifecycleState::ArgumentsComplete,
         "tui event state coalesces provider tool-call activity and pending cards when provider events omit call ids");

  ava::app::runtime::Event tool_start;
  tool_start.type = ava::app::runtime::EventType::ToolStart;
  tool_start.call_id = "call_1";
  tool_start.tool_name = "bash";
  tool_start.text = "pwd";
  tool_start.tool_arguments_json = "{\"command\":\"pwd\"}";
  ava::tui::apply_runtime_event(state, tool_start);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].call_id == "call_1" &&
             state.pending_tools[0].item.status == ava::tui::ToolTimelineStatus::Running && state.pending_tools[0].item.name == "bash" &&
             state.pending_tools[0].item.argument_summary == "pwd" && state.pending_tools[0].item.arguments_json == "{\"command\":\"pwd\"}",
         "tui event state tracks started tools by call id");

  ava::app::runtime::Event tool_progress;
  tool_progress.type = ava::app::runtime::EventType::ToolProgress;
  tool_progress.call_id = "call_1";
  tool_progress.tool_name = "bash";
  tool_progress.text = "running pwd";
  tool_progress.tool_result_json = "{\"partial\":true}";
  ava::tui::apply_runtime_event(state, tool_progress);
  auto tool_snapshot = ava::tui::event_state_transcript_snapshot(state);
  expect(state.pending_tools.size() == 1 && state.pending_tools[0].item.result_summary == "running pwd" &&
             state.pending_tools[0].item.result_json == "{\"partial\":true}" && !tool_snapshot.empty() && tool_snapshot.back().tool &&
             tool_snapshot.back().tool->status == ava::tui::ToolTimelineStatus::Running,
         "tui event state updates pending tool progress and includes it in snapshots");

  ava::app::runtime::Event tool_result;
  tool_result.type = ava::app::runtime::EventType::ToolResult;
  tool_result.call_id = "call_1";
  tool_result.tool_name = "bash";
  tool_result.status = "success";
  tool_result.text = "ok";
  tool_result.tool_result_json = "{\"ok\":true}";
  ava::tui::apply_runtime_event(state, tool_result);
  expect(state.pending_tools.empty() && !state.transcript.empty() && state.transcript.back().tool &&
             state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Success && state.transcript.back().tool->argument_summary == "pwd" &&
             state.transcript.back().tool->result_summary == "ok" && state.transcript.back().tool->arguments_json == "{\"command\":\"pwd\"}" &&
             state.transcript.back().tool->result_json == "{\"ok\":true}",
         "tui event state moves successful tool results into completed transcript items");

  ava::app::runtime::Event write_start;
  write_start.type = ava::app::runtime::EventType::ToolStart;
  write_start.call_id = "call_write";
  write_start.tool_name = "write_file";
  write_start.text = "path=src/main.cpp, content=12 bytes";
  ava::tui::apply_runtime_event(state, write_start);
  ava::app::runtime::Event write_result;
  write_result.type = ava::app::runtime::EventType::ToolResult;
  write_result.call_id = "call_write";
  write_result.tool_name = "write_file";
  write_result.status = "success";
  write_result.text = "wrote 12 bytes";
  ava::tui::apply_runtime_event(state, write_result);
  expect(!state.activity.empty() && state.activity.back().label == "write_file" && state.activity.back().status == ava::tui::ToolTimelineStatus::Success &&
             state.modified_files.size() == 1 && state.modified_files[0].path == "src/main.cpp",
         "tui event state feeds sidebar activity and modified-file summaries from successful mutating tools");

  ava::app::runtime::Event semantic_write;
  semantic_write.type = ava::app::runtime::EventType::ToolResult;
  semantic_write.call_id = "call_semantic_write";
  semantic_write.tool_name = "edit_file";
  semantic_write.status = "success";
  semantic_write.text = "edited file";
  semantic_write.changed_paths = {"src/semantic.cpp"};
  ava::tui::apply_runtime_event(state, semantic_write);
  expect(std::ranges::any_of(state.modified_files, [](ava::tui::SidebarModifiedFile const& file) { return file.path == "src/semantic.cpp"; }),
         "tui event state prefers semantic changed paths over parsing mutating tool summaries");

  ava::app::runtime::Event tool_error;
  tool_error.type = ava::app::runtime::EventType::ToolResult;
  tool_error.call_id = "call_2";
  tool_error.tool_name = "read";
  tool_error.status = "error";
  tool_error.text = "denied";
  ava::tui::apply_runtime_event(state, tool_error);
  expect(state.transcript.back().tool && state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Error &&
             state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Error && state.transcript.back().tool->result_summary == "denied",
         "tui event state records errored tool results as error tool cards");

  ava::app::runtime::Event tool_canceled_start;
  tool_canceled_start.type = ava::app::runtime::EventType::ToolStart;
  tool_canceled_start.call_id = "call_canceled";
  tool_canceled_start.tool_name = "bash";
  tool_canceled_start.text = "sleep 30";
  tool_canceled_start.tool_arguments_json = "{\"command\":\"sleep 30\"}";
  ava::tui::apply_runtime_event(state, tool_canceled_start);
  ava::app::runtime::Event tool_canceled;
  tool_canceled.type = ava::app::runtime::EventType::ToolResult;
  tool_canceled.call_id = "call_canceled";
  tool_canceled.tool_name = "bash";
  tool_canceled.status = "canceled";
  tool_canceled.text = "stopped by user";
  tool_canceled.tool_result_json = "{\"tool\":\"bash\",\"canceled\":true}";
  ava::tui::apply_runtime_event(state, tool_canceled);
  expect(state.transcript.back().tool && state.transcript.back().tool->status == ava::tui::ToolTimelineStatus::Canceled &&
             state.transcript.back().tool->lifecycle == ava::tui::ToolLifecycleState::Canceled &&
             state.transcript.back().tool->argument_summary == "sleep 30" && state.transcript.back().tool->result_summary == "stopped by user" &&
             state.activity.back().status == ava::tui::ToolTimelineStatus::Canceled,
         "tui event state records canceled tool results as canceled tool cards");

  ava::tui::TuiEventState permission_tool_state;
  ava::app::EventEnvelope permission_tool_requested{.schema_version = 1,
                                                    .event_id = "event_permission_tool_request",
                                                    .timestamp = "2026-04-30T00:00:00Z",
                                                    .session_id = "session_test",
                                                    .run_id = "run_permission_tool",
                                                    .turn_id = "turn_permission_tool",
                                                    .message_id = std::nullopt,
                                                    .request_id = "permission_1",
                                                    .correlation_id = "permission_1",
                                                    .name = "permission_requested",
                                                    .payload_json =
                                                        "{\"resolver_request_id\":\"permission_1\","
                                                        "\"permission_request_id\":\"permreq_push\","
                                                        "\"operation\":\"bash\",\"mode\":\"build\","
                                                        "\"target_path\":\"\",\"command\":\"git push origin main\","
                                                        "\"tool_name\":\"bash\",\"risk\":\"high\","
                                                        "\"reason\":\"command can change external state\"}"};
  ava::tui::apply_event_envelope(permission_tool_state, permission_tool_requested);
  ava::app::EventEnvelope permission_tool_replied{.schema_version = 1,
                                                  .event_id = "event_permission_tool_reply",
                                                  .timestamp = "2026-04-30T00:00:01Z",
                                                  .session_id = "session_test",
                                                  .run_id = "run_permission_tool",
                                                  .turn_id = "turn_permission_tool",
                                                  .message_id = std::nullopt,
                                                  .request_id = "permission_1",
                                                  .correlation_id = "permission_1",
                                                  .name = "permission_replied",
                                                  .payload_json =
                                                      "{\"resolver_request_id\":\"permission_1\","
                                                      "\"decision\":\"deny\",\"reason\":\"selected deny\"}"};
  ava::tui::apply_event_envelope(permission_tool_state, permission_tool_replied);
  ava::app::EventEnvelope permission_tool_result{.schema_version = 1,
                                                 .event_id = "event_permission_tool_result",
                                                 .timestamp = "2026-04-30T00:00:02Z",
                                                 .session_id = "session_test",
                                                 .run_id = "run_permission_tool",
                                                 .turn_id = "turn_permission_tool",
                                                 .message_id = "message_permission_tool",
                                                 .request_id = "request_tool",
                                                 .correlation_id = "call_permission_tool",
                                                 .name = "tool_result",
                                                 .payload_json =
                                                     "{\"tool_name\":\"bash\",\"text\":\"permission denied\","
                                                     "\"status\":\"error\",\"permission_request_ids\":[\"permreq_push\"],"
                                                     "\"args\":{\"command\":\"git push origin main\"},"
                                                     "\"result\":{\"tool\":\"bash\",\"exit_code\":126}}"};
  ava::tui::apply_event_envelope(permission_tool_state, permission_tool_result);
  auto permission_tool_snapshot = ava::tui::event_state_transcript_snapshot(permission_tool_state);
  auto const permission_tool_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                           .provider = "openai",
                                                                                           .model = "gpt-5.5",
                                                                                           .session_id = "session_test",
                                                                                           .input = "",
                                                                                           .status = "ready",
                                                                                           .transcript = permission_tool_snapshot,
                                                                                           .width = 88,
                                                                                           .height = 18});
  expect(permission_tool_snapshot.size() == 3 && permission_tool_snapshot.back().tool && permission_tool_snapshot.back().tool->permissions.size() == 1 &&
             permission_tool_snapshot.back().tool->permissions[0].permission_request_id == "permreq_push" &&
             permission_tool_snapshot.back().tool->permissions[0].decision == "deny" && permission_tool_snapshot.back().tool->permissions[0].risk == "high" &&
             std::ranges::any_of(permission_tool_render,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("permission deny") != std::string::npos && visible.find("risk high") != std::string::npos &&
                                          visible.find("reason command can change external state") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer attaches permission request/reply metadata to linked tool result cards");

  ava::tui::TuiEventState correlated_tool_state;
  ava::app::EventEnvelope correlated_provider_delta{.schema_version = 1,
                                                    .event_id = "event_tool_delta",
                                                    .timestamp = "2026-04-30T00:00:00Z",
                                                    .session_id = "session_test",
                                                    .run_id = "run_tool",
                                                    .turn_id = "turn_tool",
                                                    .message_id = "message_tool",
                                                    .request_id = "request_tool",
                                                    .correlation_id = "corr_tool",
                                                    .name = "provider_event",
                                                    .payload_json =
                                                        "{\"status\":\"tool_call_delta\",\"tool_name\":\"grep\","
                                                        "\"text\":\"{\\\"pattern\\\":\"}"};
  ava::tui::apply_event_envelope(correlated_tool_state, correlated_provider_delta);
  ava::app::EventEnvelope correlated_progress{.schema_version = 1,
                                              .event_id = "event_tool_progress",
                                              .timestamp = "2026-04-30T00:00:01Z",
                                              .session_id = "session_test",
                                              .run_id = "run_tool",
                                              .turn_id = "turn_tool",
                                              .message_id = "message_tool",
                                              .request_id = "request_tool",
                                              .correlation_id = "corr_tool",
                                              .name = "tool_progress",
                                              .payload_json =
                                                  "{\"tool_name\":\"grep\",\"text\":\"scanned 10 files\","
                                                  "\"status\":\"running\"}"};
  ava::tui::apply_event_envelope(correlated_tool_state, correlated_progress);
  expect(correlated_tool_state.pending_tools.size() == 1 && correlated_tool_state.pending_tools[0].call_id == "corr_tool" &&
             correlated_tool_state.pending_tools[0].request_id == "request_tool" && correlated_tool_state.pending_tools[0].correlation_id == "corr_tool" &&
             correlated_tool_state.pending_tools[0].item.result_summary == "scanned 10 files",
         "tui EventEnvelope reducer updates pending tools by backend request and correlation ids");

  ava::app::EventEnvelope correlated_result{.schema_version = 1,
                                            .event_id = "event_tool_result",
                                            .timestamp = "2026-04-30T00:00:02Z",
                                            .session_id = "session_test",
                                            .run_id = "run_tool",
                                            .turn_id = "turn_tool",
                                            .message_id = "message_tool",
                                            .request_id = "request_tool",
                                            .correlation_id = "corr_tool",
                                            .name = "tool_result",
                                            .payload_json =
                                                "{\"tool_name\":\"grep\",\"result_summary\":\"2 matches\","
                                                "\"args\":{\"pattern\":\"needle\"},"
                                                "\"result\":{\"ok\":true,\"matches\":2},"
                                                "\"status\":\"success\",\"truncated\":true,"
                                                "\"details_visible\":true,"
                                                "\"output_bytes\":256,\"total_bytes\":1024,"
                                                "\"output_lines\":4,\"total_lines\":20,"
                                                "\"start_line\":5,\"end_line\":8,\"next_offset_line\":9,"
                                                "\"omitted_bytes\":768,\"omitted_lines\":12,"
                                                "\"visible_matches\":2,\"total_matches\":8,"
                                                "\"spill_path\":\"/tmp/ava-spill/grep.txt\","
                                                "\"changed_paths\":[\"logs/output.txt\"],"
                                                "\"diff\":\"--- note.txt\\n+++ note.txt\\n-old\\n+new\","
                                                "\"diff_truncated\":true}"};
  ava::tui::apply_event_envelope(correlated_tool_state, correlated_result);
  auto const correlated_tool_render =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = ava::tui::event_state_transcript_snapshot(correlated_tool_state),
                                                           .width = 120,
                                                           .height = 14,
                                                           .tool_details_visible = false});
  expect(correlated_tool_state.pending_tools.empty() && correlated_tool_state.transcript.size() == 1 && correlated_tool_state.transcript[0].tool &&
             correlated_tool_state.transcript[0].tool->lifecycle == ava::tui::ToolLifecycleState::Complete &&
             correlated_tool_state.transcript[0].tool->truncated && correlated_tool_state.transcript[0].tool->details_visible == true &&
             correlated_tool_state.transcript[0].tool->arguments_json == "{\"pattern\":\"needle\"}" &&
             correlated_tool_state.transcript[0].tool->result_json == "{\"ok\":true,\"matches\":2}" &&
             correlated_tool_state.transcript[0].tool->changed_paths.size() == 1 &&
             correlated_tool_state.transcript[0].tool->changed_paths[0] == "logs/output.txt" &&
             correlated_tool_state.transcript[0].tool->spill_path == "/tmp/ava-spill/grep.txt" &&
             std::ranges::any_of(correlated_tool_render,
                                 [](std::string const& line) {
                                   auto const visible = strip_sgr(line);
                                   return visible.find("truncation: truncated lines 5-8/20; next offset 9") != std::string::npos &&
                                          visible.find("omitted 768 bytes, 12 lines") != std::string::npos;
                                 }) &&
             std::ranges::any_of(correlated_tool_render, [](std::string const& line) { return strip_sgr(line).find("[diff truncated]") != std::string::npos; }),
         "tui EventEnvelope reducer settles completed tools with backend-provided truncation, spill, diff, and per-tool "
         "detail metadata");

  ava::app::runtime::Event error;
  error.type = ava::app::runtime::EventType::Error;
  error.error_message = "provider failed";
  error.error_details = "Provider: provider failed";
  ava::tui::apply_runtime_event(state, error);
  expect(state.run_status == ava::tui::TuiEventRunStatus::Error && state.error_text == "provider failed" &&
             state.error_details == "Provider: provider failed" && state.transcript.back().label == "error" &&
             state.transcript.back().text == "provider failed",
         "tui event state records runtime errors and exposes error transcript text");

  ava::tui::TuiEventState streaming_error_state;
  ava::app::runtime::Event streaming_delta;
  streaming_delta.type = ava::app::runtime::EventType::MessageUpdate;
  streaming_delta.model_id = "gpt-5.5";
  streaming_delta.text = "partial answer";
  ava::tui::apply_runtime_event(streaming_error_state, streaming_delta);
  ava::app::runtime::Event streaming_error;
  streaming_error.type = ava::app::runtime::EventType::Error;
  streaming_error.model_id = "gpt-5.5";
  streaming_error.error_message = "provider: curl transport failed";
  streaming_error.error_details = "provider: curl transport failed\noutput: event: response.created\ndata: {...}";
  ava::tui::apply_runtime_event(streaming_error_state, streaming_error);
  expect(streaming_error_state.transcript.size() == 2 && streaming_error_state.transcript[0].label == "ava" &&
             streaming_error_state.transcript[0].text == "partial answer" && streaming_error_state.transcript[1].label == "error" &&
             streaming_error_state.transcript[1].text == "provider: curl transport failed" &&
             streaming_error_state.transcript[1].text.find("output: event:") == std::string::npos,
         "tui event state commits partial assistant text before compact provider error messages");
  auto const collapsed_streaming_error =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = ava::tui::event_state_transcript_snapshot(streaming_error_state),
                                                           .width = 88,
                                                           .height = 12,
                                                           .tool_details_visible = false});
  auto const expanded_streaming_error =
      ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                           .provider = "openai",
                                                           .model = "gpt-5.5",
                                                           .session_id = "session_test",
                                                           .input = "",
                                                           .status = "ready",
                                                           .transcript = ava::tui::event_state_transcript_snapshot(streaming_error_state),
                                                           .width = 88,
                                                           .height = 16,
                                                           .tool_details_visible = true});
  expect(std::ranges::any_of(collapsed_streaming_error, [](std::string const& line) { return strip_sgr(line).find("partial answer") != std::string::npos; }) &&
             std::ranges::any_of(collapsed_streaming_error,
                                 [](std::string const& line) { return strip_sgr(line).find("! provider: curl transport failed") != std::string::npos; }) &&
             std::ranges::any_of(collapsed_streaming_error,
                                 [](std::string const& line) { return strip_sgr(line).find("details hidden · /details") != std::string::npos; }) &&
             std::ranges::none_of(collapsed_streaming_error,
                                  [](std::string const& line) { return strip_sgr(line).find("output: event:") != std::string::npos; }) &&
             std::ranges::any_of(expanded_streaming_error,
                                 [](std::string const& line) { return strip_sgr(line).find("output: event: response.created") != std::string::npos; }),
         "tui renders concise collapsed errors while preserving partial assistant text and details-on-demand");

  ava::tui::TuiEventState canceled_state;
  ava::app::runtime::Event canceled;
  canceled.type = ava::app::runtime::EventType::Error;
  canceled.error_message = "agent loop canceled";
  canceled.error_details = "Unknown: agent loop canceled";
  ava::tui::apply_runtime_event(canceled_state, canceled);
  expect(canceled_state.run_status == ava::tui::TuiEventRunStatus::Canceled && canceled_state.error_text == "stopped by user" &&
             canceled_state.transcript.size() == 1 && canceled_state.transcript[0].label == "ava" &&
             canceled_state.transcript[0].text == "stopped by user. Submit a new prompt to continue." && !canceled_state.activity.empty() &&
             canceled_state.activity.back().label == "stopped" && canceled_state.activity.back().status == ava::tui::ToolTimelineStatus::Canceled &&
             canceled_state.activity.back().detail.find("submit a new prompt to continue") != std::string::npos,
         "tui event state presents cooperative cancellation with continuation guidance");

  ava::tui::TuiEventState explicit_canceled_state;
  ava::app::runtime::Event explicit_canceled;
  explicit_canceled.type = ava::app::runtime::EventType::Canceled;
  explicit_canceled.text = "stopped by user";
  explicit_canceled.reason = "cancel_requested";
  ava::tui::apply_runtime_event(explicit_canceled_state, explicit_canceled);
  expect(explicit_canceled_state.run_status == ava::tui::TuiEventRunStatus::Canceled && explicit_canceled_state.transcript.size() == 1 &&
             explicit_canceled_state.transcript[0].text == "stopped by user. Submit a new prompt to continue." &&
             explicit_canceled_state.activity.back().status == ava::tui::ToolTimelineStatus::Canceled &&
             explicit_canceled_state.activity.back().detail.find("cancel_requested") != std::string::npos &&
             explicit_canceled_state.activity.back().detail.find("submit a new prompt to continue") != std::string::npos,
         "tui event state accepts explicit backend canceled lifecycle events");

  ava::tui::TuiEventState lifecycle_state;
  ava::app::runtime::Event compaction_start;
  compaction_start.type = ava::app::runtime::EventType::CompactionStart;
  compaction_start.trigger = "auto";
  compaction_start.estimated_tokens = 9000;
  compaction_start.threshold_tokens = 8000;
  ava::tui::apply_runtime_event(lifecycle_state, compaction_start);
  expect(lifecycle_state.transcript.empty() && lifecycle_state.activity.size() == 1 && lifecycle_state.activity[0].label == "compaction" &&
             lifecycle_state.activity[0].detail.find("tokens~9000/8000") != std::string::npos,
         "tui event state keeps compaction starts in status activity without inventing transcript content");
  ava::app::runtime::Event retry;
  retry.type = ava::app::runtime::EventType::Retry;
  retry.reason = "context_overflow";
  retry.trigger = "context_overflow";
  retry.attempt = 1;
  retry.max_attempts = 1;
  retry.delay_ms = 250;
  retry.estimated_tokens = 9000;
  retry.threshold_tokens = 8000;
  retry.snapshot_entries = 3;
  retry.current_entries = 4;
  ava::tui::apply_runtime_event(lifecycle_state, retry);
  ava::app::runtime::Event retry_tick;
  retry_tick.type = ava::app::runtime::EventType::RetryTick;
  retry_tick.reason = "context_overflow";
  retry_tick.trigger = "context_overflow";
  retry_tick.attempt = 1;
  retry_tick.max_attempts = 1;
  retry_tick.delay_ms = 250;
  retry_tick.remaining_ms = 125;
  ava::tui::apply_runtime_event(lifecycle_state, retry_tick);
  ava::app::runtime::Event retry_tick_update = retry_tick;
  retry_tick_update.remaining_ms = 25;
  ava::tui::apply_runtime_event(lifecycle_state, retry_tick_update);
  ava::app::runtime::Event compaction_end;
  compaction_end.type = ava::app::runtime::EventType::CompactionEnd;
  compaction_end.trigger = "context_overflow";
  compaction_end.attempt = 1;
  compaction_end.max_attempts = 2;
  compaction_end.summary_bytes = 1234;
  ava::tui::apply_runtime_event(lifecycle_state, compaction_end);
  expect(lifecycle_state.transcript.size() == 3 && lifecycle_state.transcript[0].label == "audit" &&
             lifecycle_state.transcript[0].text.find("retrying after context_overflow") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("attempt 1/1") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("delay=250ms") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("tokens~9000/8000") != std::string::npos &&
             lifecycle_state.transcript[0].text.find("entries=3/4") != std::string::npos && lifecycle_state.transcript[1].label == "audit" &&
             lifecycle_state.transcript[1].text.find("retry countdown after context_overflow") != std::string::npos &&
             lifecycle_state.transcript[1].text.find("remaining=25ms") != std::string::npos &&
             lifecycle_state.transcript[1].text.find("remaining=125ms") == std::string::npos && lifecycle_state.transcript[2].label == "compaction" &&
             lifecycle_state.transcript[2].text.find("compaction completed") != std::string::npos &&
             lifecycle_state.transcript[2].text.find("attempt 1/2") != std::string::npos &&
             lifecycle_state.transcript[2].text.find("summary=1234 bytes") != std::string::npos &&
             std::ranges::any_of(lifecycle_state.activity,
                                 [](ava::tui::SidebarActivityItem const& activity) {
                                   return activity.label == "retry" && activity.detail.find("remaining=25ms") != std::string::npos;
                                 }),
         "tui event state renders backend retry, retry countdown, and compaction markers with backend-provided detail");
  auto const compaction_render = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                                      .provider = "openai",
                                                                                      .model = "gpt-5.5",
                                                                                      .session_id = "session_test",
                                                                                      .input = "",
                                                                                      .status = "ready",
                                                                                      .transcript = ava::tui::event_state_transcript_snapshot(lifecycle_state),
                                                                                      .width = 88,
                                                                                      .height = 12});
  expect(std::ranges::any_of(compaction_render,
                             [](std::string const& line) {
                               auto const visible = strip_sgr(line);
                               return visible.find("compact compaction completed") != std::string::npos &&
                                      visible.find("summary=1234 bytes") != std::string::npos;
                             }),
         "tui renders compaction lifecycle entries as dedicated long-session status cards");

  ava::tui::TuiEventState done_state;
  delta.text = "done text";
  ava::tui::apply_runtime_event(done_state, delta);
  ava::app::runtime::Event done;
  done.type = ava::app::runtime::EventType::Done;
  done.stop_reason = "stop";
  done.provider_iterations = 2;
  done.tool_calls = 1;
  ava::tui::apply_runtime_event(done_state, done);
  expect(done_state.run_status == ava::tui::TuiEventRunStatus::Done && done_state.stop_reason == "stop" && done_state.provider_iterations == 2 &&
             done_state.tool_calls == 1 && done_state.pending_assistant_text.empty() && done_state.transcript.size() == 1 &&
             done_state.transcript[0].text == "done text",
         "tui event state records done metadata and commits pending assistant text");

  std::vector<ava::app::runtime::Event> live_events;
  ava::app::runtime::Event parity_session;
  parity_session.type = ava::app::runtime::EventType::SessionStart;
  parity_session.provider_id = "openai";
  parity_session.model_id = "gpt-5.5";
  live_events.push_back(parity_session);
  ava::app::runtime::Event parity_user;
  parity_user.type = ava::app::runtime::EventType::UserMessage;
  parity_user.text = "inspect";
  live_events.push_back(parity_user);
  ava::app::runtime::Event parity_retry;
  parity_retry.type = ava::app::runtime::EventType::Retry;
  parity_retry.reason = "context_overflow";
  parity_retry.trigger = "context_overflow";
  parity_retry.attempt = 1;
  parity_retry.max_attempts = 1;
  live_events.push_back(parity_retry);
  ava::app::runtime::Event parity_retry_tick;
  parity_retry_tick.type = ava::app::runtime::EventType::RetryTick;
  parity_retry_tick.reason = "context_overflow";
  parity_retry_tick.trigger = "context_overflow";
  parity_retry_tick.attempt = 1;
  parity_retry_tick.max_attempts = 1;
  parity_retry_tick.delay_ms = 250;
  parity_retry_tick.remaining_ms = 0;
  live_events.push_back(parity_retry_tick);
  ava::app::runtime::Event parity_compaction_start;
  parity_compaction_start.type = ava::app::runtime::EventType::CompactionStart;
  parity_compaction_start.trigger = "context_overflow";
  parity_compaction_start.attempt = 1;
  parity_compaction_start.max_attempts = 2;
  live_events.push_back(parity_compaction_start);
  ava::app::runtime::Event parity_compaction_end;
  parity_compaction_end.type = ava::app::runtime::EventType::CompactionEnd;
  parity_compaction_end.trigger = "context_overflow";
  parity_compaction_end.attempt = 1;
  parity_compaction_end.max_attempts = 2;
  parity_compaction_end.summary_bytes = 512;
  live_events.push_back(parity_compaction_end);
  ava::app::runtime::Event parity_reasoning;
  parity_reasoning.type = ava::app::runtime::EventType::ReasoningDelta;
  parity_reasoning.text = "checking";
  live_events.push_back(parity_reasoning);
  ava::app::runtime::Event parity_delta;
  parity_delta.type = ava::app::runtime::EventType::MessageUpdate;
  parity_delta.model_id = "gpt-5.5";
  parity_delta.text = "answer";
  live_events.push_back(parity_delta);
  ava::app::runtime::Event parity_end;
  parity_end.type = ava::app::runtime::EventType::MessageEnd;
  parity_end.model_id = "gpt-5.5";
  live_events.push_back(parity_end);
  ava::app::runtime::Event parity_tool_start;
  parity_tool_start.type = ava::app::runtime::EventType::ToolStart;
  parity_tool_start.call_id = "call_parity";
  parity_tool_start.tool_name = "read_file";
  parity_tool_start.text = "path=README.md";
  live_events.push_back(parity_tool_start);
  ava::app::runtime::Event parity_tool_progress;
  parity_tool_progress.type = ava::app::runtime::EventType::ToolProgress;
  parity_tool_progress.call_id = "call_parity";
  parity_tool_progress.tool_name = "read_file";
  parity_tool_progress.text = "reading";
  parity_tool_progress.status = "running";
  live_events.push_back(parity_tool_progress);
  ava::app::runtime::Event parity_tool_result;
  parity_tool_result.type = ava::app::runtime::EventType::ToolResult;
  parity_tool_result.call_id = "call_parity";
  parity_tool_result.tool_name = "read_file";
  parity_tool_result.text = "12 bytes";
  parity_tool_result.status = "success";
  live_events.push_back(parity_tool_result);
  ava::app::runtime::Event parity_audit;
  parity_audit.type = ava::app::runtime::EventType::ProviderEvent;
  parity_audit.status = "tui:question_answer";
  parity_audit.text = "question answered: yes";
  live_events.push_back(parity_audit);
  ava::app::runtime::Event parity_done;
  parity_done.type = ava::app::runtime::EventType::Done;
  parity_done.stop_reason = "stop";
  parity_done.provider_iterations = 1;
  parity_done.tool_calls = 1;
  live_events.push_back(parity_done);

  ava::tui::TuiEventState live_state;
  ava::tui::TuiEventState replayed_state;
  ava::app::EventEnvelopeContext parity_context;
  parity_context.run_id = "run_1";
  parity_context.turn_id = "turn_1";
  parity_context.message_id = "message_1";
  parity_context.request_id = "request_1";
  parity_context.correlation_id = "correlation_1";
  for (auto const& event : live_events)
  {
    ava::tui::apply_runtime_event(live_state, event);
    ava::tui::apply_event_envelope(replayed_state, ava::app::to_event_envelope(event, parity_context));
  }
  auto visible_lines = [](std::vector<std::string> const& rendered) {
    std::vector<std::string> visible;
    visible.reserve(rendered.size());
    for (auto const& line : rendered) visible.push_back(strip_sgr(line));
    return visible;
  };
  auto const live_render =
      visible_lines(ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                         .provider = "openai",
                                                                         .model = "gpt-5.5",
                                                                         .session_id = "session_test",
                                                                         .input = "",
                                                                         .status = "ready",
                                                                         .transcript = ava::tui::event_state_transcript_snapshot(live_state),
                                                                         .width = 72,
                                                                         .height = 20}));
  auto const replayed_render =
      visible_lines(ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                         .provider = "openai",
                                                                         .model = "gpt-5.5",
                                                                         .session_id = "session_test",
                                                                         .input = "",
                                                                         .status = "ready",
                                                                         .transcript = ava::tui::event_state_transcript_snapshot(replayed_state),
                                                                         .width = 72,
                                                                         .height = 20}));
  expect(live_render == replayed_render && replayed_state.active_run_id == "run_1" && replayed_state.active_turn_id == "turn_1" &&
             replayed_state.active_message_id == "message_1" && replayed_state.active_request_id == "request_1" &&
             replayed_state.active_correlation_id == "correlation_1",
         "tui EventEnvelope replay renders the same visible transcript story as live runtime::Event reduction and tracks "
         "backend ids");

  ava::tui::TuiEventState resolver_state;
  ava::app::EventEnvelope permission_requested{.schema_version = 1,
                                               .event_id = "event_permission",
                                               .timestamp = "2026-04-30T00:00:00Z",
                                               .session_id = "session_test",
                                               .run_id = "run_prompt",
                                               .turn_id = "turn_prompt",
                                               .message_id = std::nullopt,
                                               .request_id = "request_prompt",
                                               .correlation_id = "request_prompt",
                                               .name = "permission_requested",
                                               .payload_json =
                                                   "{\"resolver_request_id\":\"permission_1\","
                                                   "\"operation\":\"shell.run\",\"mode\":\"build\","
                                                   "\"target_path\":\"\",\"command\":\"pwd\","
                                                   "\"tool_name\":\"bash\",\"reason\":\"needs approval\"}"};
  ava::tui::apply_event_envelope(resolver_state, permission_requested);
  expect(resolver_state.transcript.size() == 1 && resolver_state.transcript[0].label == "audit" &&
             resolver_state.transcript[0].text.find("permission requested: bash pwd") != std::string::npos && !resolver_state.activity.empty() &&
             resolver_state.activity[0].label == "permission" && resolver_state.active_run_id == "run_prompt",
         "tui EventEnvelope reducer records shared permission request envelopes without inventing prompt decisions");

  ava::app::EventEnvelope permission_replied{.schema_version = 1,
                                             .event_id = "event_permission_reply",
                                             .timestamp = "2026-04-30T00:00:00Z",
                                             .session_id = "session_test",
                                             .run_id = "run_prompt",
                                             .turn_id = "turn_prompt",
                                             .message_id = std::nullopt,
                                             .request_id = "request_prompt",
                                             .correlation_id = "request_prompt",
                                             .name = "permission_replied",
                                             .payload_json =
                                                 "{\"resolver_request_id\":\"permission_1\","
                                                 "\"decision\":\"deny\"}"};
  ava::tui::apply_event_envelope(resolver_state, permission_replied);
  expect(resolver_state.transcript.size() == 2 && resolver_state.transcript[1].label == "audit" && resolver_state.transcript[1].text == "permission replied" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) { return item.label == "permission" && item.detail == "permission replied"; }),
         "tui EventEnvelope reducer records shared permission reply envelopes");

  ava::app::EventEnvelope question_requested{.schema_version = 1,
                                             .event_id = "event_question",
                                             .timestamp = "2026-04-30T00:00:01Z",
                                             .session_id = "session_test",
                                             .run_id = "run_prompt",
                                             .turn_id = "turn_prompt",
                                             .message_id = std::nullopt,
                                             .request_id = "request_question",
                                             .correlation_id = "request_question",
                                             .name = "question_requested",
                                             .payload_json = "{\"question\":\"Pick an option\"}"};
  ava::tui::apply_event_envelope(resolver_state, question_requested);
  expect(resolver_state.transcript.size() == 3 && resolver_state.transcript[2].label == "audit" &&
             resolver_state.transcript[2].text == "question requested: Pick an option" &&
             std::ranges::any_of(
                 resolver_state.activity,
                 [](ava::tui::SidebarActivityItem const& item) { return item.label == "question" && item.detail.find("Pick an option") != std::string::npos; }),
         "tui EventEnvelope reducer records shared question request envelopes without inventing prompt answers");

  ava::app::EventEnvelope question_replied{.schema_version = 1,
                                           .event_id = "event_question_reply",
                                           .timestamp = "2026-04-30T00:00:01Z",
                                           .session_id = "session_test",
                                           .run_id = "run_prompt",
                                           .turn_id = "turn_prompt",
                                           .message_id = std::nullopt,
                                           .request_id = "request_question",
                                           .correlation_id = "request_question",
                                           .name = "question_replied",
                                           .payload_json =
                                               "{\"resolver_request_id\":\"question_1\","
                                               "\"answer\":\"custom ok\"}"};
  ava::tui::apply_event_envelope(resolver_state, question_replied);
  expect(resolver_state.transcript.size() == 4 && resolver_state.transcript[3].label == "audit" && resolver_state.transcript[3].text == "question replied" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) { return item.label == "question" && item.detail == "question replied"; }),
         "tui EventEnvelope reducer records shared question reply envelopes");

  ava::app::EventEnvelope steer_queued{.schema_version = 1,
                                       .event_id = "event_steer",
                                       .timestamp = "2026-04-30T00:00:02Z",
                                       .session_id = "session_test",
                                       .run_id = "run_prompt",
                                       .turn_id = "turn_prompt",
                                       .message_id = std::nullopt,
                                       .request_id = "request_steer",
                                       .correlation_id = "request_steer",
                                       .name = "steer_queued",
                                       .payload_json = "{\"message\":\"Use smaller patch groups\"}"};
  ava::tui::apply_event_envelope(resolver_state, steer_queued);
  expect(resolver_state.transcript.size() == 5 && resolver_state.transcript.back().label == "audit" &&
             resolver_state.transcript.back().text.find("steer queued") != std::string::npos &&
             resolver_state.transcript.back().text.find("Use smaller patch groups") != std::string::npos && resolver_state.queued_messages.size() == 1 &&
             resolver_state.queued_messages.back().kind == "steer" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "steer" && item.detail.find("Use smaller patch groups") != std::string::npos &&
                                          item.status == ava::tui::ToolTimelineStatus::Running;
                                 }),
         "tui EventEnvelope reducer surfaces backend queued steer events in transcript and sidebar activity");

  ava::app::EventEnvelope follow_up_queued{.schema_version = 1,
                                           .event_id = "event_follow_queue",
                                           .timestamp = "2026-04-30T00:00:02Z",
                                           .session_id = "session_test",
                                           .run_id = "run_prompt",
                                           .turn_id = "turn_prompt",
                                           .message_id = std::nullopt,
                                           .request_id = "request_follow",
                                           .correlation_id = "request_steer",
                                           .name = "follow_up_queued",
                                           .payload_json = "{\"message\":\"Continue after tests\"}"};
  ava::tui::apply_event_envelope(resolver_state, follow_up_queued);
  expect(resolver_state.transcript.size() == 6 && resolver_state.transcript.back().text.find("follow-up queued") != std::string::npos &&
             resolver_state.queued_messages.size() == 2 && resolver_state.queued_messages.back().kind == "follow-up" &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" && item.status == ava::tui::ToolTimelineStatus::Running &&
                                          item.detail.find("Continue after tests") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer records backend queued follow-up events");

  ava::app::EventEnvelope follow_up_started{.schema_version = 1,
                                            .event_id = "event_follow_start",
                                            .timestamp = "2026-04-30T00:00:02Z",
                                            .session_id = "session_test",
                                            .run_id = "run_prompt",
                                            .turn_id = "turn_prompt",
                                            .message_id = std::nullopt,
                                            .request_id = "request_follow",
                                            .correlation_id = "request_follow",
                                            .name = "follow_up_started",
                                            .payload_json = "{\"message\":\"Continue after tests\"}"};
  ava::tui::apply_event_envelope(resolver_state, follow_up_started);
  expect(resolver_state.transcript.size() == 7 && resolver_state.transcript.back().text.find("follow-up started") != std::string::npos &&
             resolver_state.queued_messages.size() == 1 &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" && item.status == ava::tui::ToolTimelineStatus::Running &&
                                          item.detail.find("follow-up started") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer records backend follow-up start events");

  ava::app::EventEnvelope follow_up_skipped{.schema_version = 1,
                                            .event_id = "event_follow_skip",
                                            .timestamp = "2026-04-30T00:00:02Z",
                                            .session_id = "session_test",
                                            .run_id = "run_prompt",
                                            .turn_id = "turn_prompt",
                                            .message_id = std::nullopt,
                                            .request_id = "request_follow",
                                            .correlation_id = "request_follow",
                                            .name = "follow_up_skipped",
                                            .payload_json =
                                                "{\"message\":\"Continue after tests\","
                                                "\"reason\":\"canceled\","
                                                "\"message_truncated\":true,"
                                                "\"message_bytes\":4096}"};
  ava::tui::apply_event_envelope(resolver_state, follow_up_skipped);
  expect(resolver_state.transcript.size() == 8 &&
             resolver_state.transcript.back().text.find("follow-up skipped: run stopped before delivery; submit it again to continue") != std::string::npos &&
             resolver_state.transcript.back().text.find("message truncated from 4096 bytes") != std::string::npos &&
             std::ranges::any_of(resolver_state.activity,
                                 [](ava::tui::SidebarActivityItem const& item) {
                                   return item.label == "follow-up" && item.status == ava::tui::ToolTimelineStatus::Error &&
                                          item.detail.find("Continue after tests") != std::string::npos;
                                 }),
         "tui EventEnvelope reducer records skipped follow-up events with continuation guidance and truncation metadata");

  ava::tui::TuiEventState steering_skip_state;
  ava::app::EventEnvelope steer_skipped{.schema_version = 1,
                                        .event_id = "event_steer_skip",
                                        .timestamp = "2026-04-30T00:00:02Z",
                                        .session_id = "session_test",
                                        .run_id = "run_prompt",
                                        .turn_id = "turn_prompt",
                                        .message_id = std::nullopt,
                                        .request_id = "request_steer",
                                        .correlation_id = "request_steer",
                                        .name = "steer_skipped",
                                        .payload_json =
                                            "{\"message\":\"Use smaller patch groups\","
                                            "\"reason\":\"run_completed_before_safe_point\"}"};
  ava::tui::apply_event_envelope(steering_skip_state, steer_skipped);
  expect(steering_skip_state.transcript.size() == 1 &&
             steering_skip_state.transcript.back().text.find("steer skipped: run finished before the next safe steering point") != std::string::npos &&
             steering_skip_state.transcript.back().text.find("Use smaller patch groups") != std::string::npos,
         "tui EventEnvelope reducer explains skipped steering when the turn finishes before a safe point");

  ava::app::EventEnvelope cancel_requested{.schema_version = 1,
                                           .event_id = "event_cancel",
                                           .timestamp = "2026-04-30T00:00:03Z",
                                           .session_id = "session_test",
                                           .run_id = "run_prompt",
                                           .turn_id = "turn_prompt",
                                           .message_id = std::nullopt,
                                           .request_id = "cancel_request",
                                           .correlation_id = "request_prompt",
                                           .name = "cancel_requested",
                                           .payload_json =
                                               "{\"active_run\":true,\"cleared_steer\":1,"
                                               "\"cleared_follow_up\":2,\"active_request_id\":\"request_prompt\"}"};
  ava::tui::apply_event_envelope(resolver_state, cancel_requested);
  expect(resolver_state.transcript.back().label == "audit" &&
             resolver_state.transcript.back().text.find("cancel requested for active run") != std::string::npos &&
             resolver_state.transcript.back().text.find("steer=1 follow-up=2") != std::string::npos && resolver_state.activity.back().label == "cancel",
         "tui EventEnvelope reducer surfaces backend cancel requests without pretending the run has finished");
}

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
};

struct VirtualTerminalResult
{
  bool screen_created = false;
  bool size_reported = false;
  bool base_drawn = false;
  bool modal_drawn = false;
  bool cursor_restored_after_modal = false;
};

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
    int rows = 0;
    int columns = 0;
    getmaxyx(stdscr, rows, columns);
    result.size_reported = rows > 0 && columns > 0;
    expect(result.size_reported, "ncurses smoke test reports a usable virtual screen size for " + profile.name);
    static_cast<void>(resizeterm(14, 64));
    auto const ime_sensitive_input = std::string("a") + "\xE7\x95\x8C" + "e" + "\xCC\x81";
    auto const snapshot =
        ava::tui::ComposerSnapshot{.mode = "build",
                                   .provider = "openai",
                                   .model = "gpt-5.5",
                                   .session_id = "session_term",
                                   .input = ime_sensitive_input,
                                   .status = "ready",
                                   .transcript = {ava::tui::TranscriptItem{.label = "ava", .text = "virtual terminal draw keeps \xE7\x95\x8C bounded"}},
                                   .width = 64,
                                   .height = 14,
                                   .input_cursor = ime_sensitive_input.size()};
    auto const expected_column = ava::tui::detail::input_cursor_column(snapshot, ava::tui::composer_main_width(snapshot));
    result.base_drawn = ava::tui::draw_screen(snapshot);

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
  for (auto const& profile : profiles)
  {
    auto const result = exercise_virtual_terminal_profile(profile);
    if (result.screen_created)
      ++exercised;
    expect(result.screen_created, "ncurses smoke test creates a screen without a real terminal for " + profile.name);
    expect(result.base_drawn && result.modal_drawn && result.cursor_restored_after_modal,
           "ncurses smoke test draws base/modal frames and restores the composer cursor for " + profile.name);
  }
  expect(exercised == profiles.size(),
         "ncurses smoke test covers xterm and screen terminfo plus tmux, kitty, wezterm, and ssh-like environment "
         "variables");
  static_cast<void>(std::setlocale(LC_ALL, previous_locale.c_str()));
}

void test_tui_large_render_performance_budget()
{
  std::vector<ava::tui::TranscriptItem> transcript;
  transcript.reserve(900);
  for (int index = 0; index < 300; ++index)
  {
    transcript.push_back(ava::tui::TranscriptItem{
        .label = "you",
        .text = "performance input " + std::to_string(index) + " with CJK \xE7\x95\x8C combining e\xCC\x81 and a very-long-token-that-must-wrap-safely"});
    transcript.push_back(
        ava::tui::TranscriptItem{.label = "ava",
                                 .text = "performance answer " + std::to_string(index) + " keeps rendered rows bounded while the transcript is large",
                                 .meta = "Build · GPT-5.5",
                                 .thinking = "reasoning summary " + std::to_string(index)});
    transcript.push_back(ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                     .name = "read_file",
                                                                                     .argument_summary = "path=src/ava/tui/composer.cpp",
                                                                                     .result_summary = "read 1024 bytes",
                                                                                     .call_id = "perf_" + std::to_string(index),
                                                                                     .lifecycle = ava::tui::ToolLifecycleState::Complete}});
  }

  auto const start = std::chrono::steady_clock::now();
  std::vector<std::string> frame;
  for (int pass = 0; pass < 4; ++pass)
  {
    frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                 .provider = "openai",
                                                                 .model = "gpt-5.5",
                                                                 .session_id = "session_perf",
                                                                 .input = "draft \xE7\x95\x8C",
                                                                 .status = "ready",
                                                                 .processing = true,
                                                                 .transcript = transcript,
                                                                 .transcript_scroll_offset = static_cast<std::size_t>(pass * 20),
                                                                 .width = 120,
                                                                 .height = 36,
                                                                 .input_cursor = std::string::npos,
                                                                 .tool_details_visible = true,
                                                                 .thinking_visible = true});
  }
  auto const elapsed = std::chrono::steady_clock::now() - start;
  std::size_t max_columns = 0;
  std::string widest_line;
  for (auto const& line : frame)
  {
    auto const columns = visible_columns(line);
    if (columns > max_columns)
    {
      max_columns = columns;
      widest_line = strip_sgr(line);
    }
  }
  if (max_columns > 120)
  {
    std::cerr << "tui large render widest line has " << max_columns << " columns: " << widest_line << '\n';
  }
  expect(frame.size() == 36, "tui large render performance frame keeps the requested height");
  expect(std::ranges::all_of(frame, [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 120; }),
         "tui large render performance frame keeps every rendered line inside the requested width");
  expect(elapsed < std::chrono::seconds(5), "tui large render performance budget catches pathological redraw slowdowns without a real terminal");
}

void test_tui_large_tool_output_preview_is_bounded()
{
  auto const small_output_card = ava::tui::detail::render_tool_card(ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                               .name = "grep",
                                                                                               .result_summary = "4 matches",
                                                                                               .result_json = "{\"output\":\"one\\ntwo\\nthree\\nfour\"}",
                                                                                               .lifecycle = ava::tui::ToolLifecycleState::Complete},
                                                                    96, false);
  expect(std::ranges::any_of(small_output_card,
                             [](std::string const& line) { return strip_sgr(line).find("output: 2 shown/4 lines · 2 hidden") != std::string::npos; }),
         "tui output preview keeps exact hidden-line counts for small unmetadata tool output");

  constexpr auto total_lines = std::size_t{20000};
  std::string output_json = "{\"output\":\"";
  output_json.reserve(total_lines * 28);
  for (std::size_t index = 0; index < total_lines; ++index)
  {
    if (index > 0)
      output_json += "\\n";
    output_json += "large output line ";
    output_json += std::to_string(index);
  }
  output_json += "\"}";

  auto item = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                         .name = "grep",
                                         .argument_summary = "pattern=large",
                                         .result_summary = "20000 matches",
                                         .result_json = std::move(output_json),
                                         .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                         .total_lines = total_lines};

  auto const start = std::chrono::steady_clock::now();
  auto const compact = ava::tui::detail::render_tool_card(item, 96, false);
  item.details_visible = true;
  auto const expanded = ava::tui::detail::render_tool_card(item, 96, true);
  auto const elapsed = std::chrono::steady_clock::now() - start;

  std::string compact_text;
  for (auto const& line : compact)
  {
    compact_text += strip_sgr(line);
    compact_text += '\n';
  }
  std::string expanded_text;
  for (auto const& line : expanded)
  {
    expanded_text += strip_sgr(line);
    expanded_text += '\n';
  }

  expect(compact_text.find("output: 2 shown/20000 lines · 19998 hidden") != std::string::npos &&
             compact_text.find("large output line 0") != std::string::npos && compact_text.find("large output line 1") != std::string::npos &&
             compact_text.find("large output line 19999") == std::string::npos,
         "tui collapsed output preview uses backend totals without rendering deep large-output lines");
  expect(expanded_text.find("output: 8 shown/20000 lines · 19992 hidden") != std::string::npos &&
             expanded_text.find("large output line 7") != std::string::npos && expanded_text.find("large output line 8") == std::string::npos &&
             expanded_text.find("large output line 19999") == std::string::npos &&
             std::ranges::all_of(expanded, [](std::string const& line) { return visible_columns(line) <= 96; }),
         "tui expanded output preview remains bounded for large tool output");
  expect(elapsed < std::chrono::seconds(2), "tui large tool-output preview avoids pathological redraw cost");
}

void test_tui_transcript_tail_renderer_matches_full_visible_window()
{
  std::vector<ava::tui::TranscriptItem> transcript;
  for (int index = 0; index < 48; ++index)
  {
    transcript.push_back(ava::tui::TranscriptItem{
        .label = "you", .text = "tail renderer input " + std::to_string(index) + " with enough text to wrap over multiple terminal columns"});
    transcript.push_back(
        ava::tui::TranscriptItem{.label = "ava",
                                 .text = "tail renderer answer " + std::to_string(index) + " keeps viewport rows equivalent to the full renderer",
                                 .meta = "Build · GPT-5.5",
                                 .thinking = index % 3 == 0 ? "thinking " + std::to_string(index) : std::string{}});
    if (index % 4 == 0)
    {
      transcript.push_back(ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                       .name = "read_file",
                                                                                       .argument_summary = "path=src/main.cpp",
                                                                                       .result_summary = "read 12 lines",
                                                                                       .call_id = "tail_read_" + std::to_string(index),
                                                                                       .lifecycle = ava::tui::ToolLifecycleState::Complete}});
      transcript.push_back(ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                       .name = "grep",
                                                                                       .argument_summary = "pattern=tail",
                                                                                       .result_summary = "2 matches",
                                                                                       .call_id = "tail_grep_" + std::to_string(index),
                                                                                       .lifecycle = ava::tui::ToolLifecycleState::Complete}});
    }
  }

  constexpr auto width = std::size_t{92};
  constexpr auto height = std::size_t{13};
  auto const full = ava::tui::detail::render_transcript_lines(transcript, width, true, true);
  for (auto const offset : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{32}, std::size_t{96}})
  {
    auto const expected = ava::tui::detail::visible_transcript_lines(full, width, height, offset);
    auto const tail_budget = height + offset;
    auto const tail = ava::tui::detail::render_transcript_tail_lines(transcript, width, tail_budget, true, true);
    auto const actual = ava::tui::detail::visible_transcript_lines(tail, width, height, offset);
    expect(actual == expected, "tui bounded transcript tail renderer matches full-render visible rows at scroll offset " + std::to_string(offset));
  }
}

void test_tui_active_nonblocking_command_lane()
{
  ava::tui::TuiActiveRunQueues queues;
  std::size_t calls = 0;
  queues.run_nonblocking_command = [&calls](std::string const& submitted) -> std::optional<std::vector<std::string>> {
    if (!submitted.starts_with("/jobs"))
      return std::nullopt;
    ++calls;
    return std::vector<std::string>{"promoted without modal"};
  };
  auto promoted = ava::tui::dispatch_tui_active_nonblocking_command(queues, "/jobs promote job_1");
  auto unrelated = ava::tui::dispatch_tui_active_nonblocking_command(queues, "/models");
  ava::tui::TuiActiveRunQueues unavailable;
  auto missing = ava::tui::dispatch_tui_active_nonblocking_command(unavailable, "/jobs");
  expect(promoted && *promoted == std::vector<std::string>{"promoted without modal"} && !unrelated && !missing && calls == 1,
         "tui active command lane dispatches eligible /jobs output in place without modal state and leaves unrelated commands queued between turns");
}

void test_tui_very_long_transcript_performance_budget()
{
  std::vector<ava::tui::TranscriptItem> transcript;
  transcript.reserve(1400);
  for (int index = 0; index < 600; ++index)
  {
    transcript.push_back(ava::tui::TranscriptItem{
        .label = "you",
        .text = "long transcript input " + std::to_string(index) + " asks for a bounded terminal redraw with CJK \xE7\x95\x8C and a long-token-that-wraps"});
    transcript.push_back(ava::tui::TranscriptItem{
        .label = "ava",
        .text = "long transcript answer " + std::to_string(index) + " renders markdown like **bold**, `code`, and - bullet-shaped text without overflowing",
        .meta = "Build · GPT-5.5",
        .thinking = index % 4 == 0 ? "checked viewport stress path " + std::to_string(index) : std::string{}});
    if (index % 5 == 0)
    {
      transcript.push_back(ava::tui::TranscriptItem{.tool = ava::tui::ToolTimelineItem{.status = ava::tui::ToolTimelineStatus::Success,
                                                                                       .name = "bash",
                                                                                       .argument_summary = "command=ctest --test-dir build",
                                                                                       .result_summary = "exit=0 lines=42 hidden=120",
                                                                                       .call_id = "long_perf_" + std::to_string(index),
                                                                                       .lifecycle = ava::tui::ToolLifecycleState::Complete,
                                                                                       .truncated = true,
                                                                                       .output_lines = 42,
                                                                                       .total_lines = 162,
                                                                                       .omitted_lines = 120}});
    }
  }
  expect(transcript.size() > 900, "tui very long transcript stress extends beyond the existing large-render scale");

  auto const start = std::chrono::steady_clock::now();
  std::vector<std::string> frame;
  std::vector<std::string> visible_frames;
  for (int pass = 0; pass < 3; ++pass)
  {
    frame = ava::tui::render_composer(ava::tui::ComposerSnapshot{.mode = "build",
                                                                 .provider = "openai",
                                                                 .model = "gpt-5.5",
                                                                 .session_id = "session_long_perf",
                                                                 .input = "draft stays responsive while scrolling a very long transcript",
                                                                 .status = "ready",
                                                                 .processing = true,
                                                                 .transcript = transcript,
                                                                 .transcript_scroll_offset = static_cast<std::size_t>(pass * 240),
                                                                 .width = 96,
                                                                 .height = 30,
                                                                 .input_cursor = std::string::npos,
                                                                 .tool_details_visible = true,
                                                                 .thinking_visible = true});
    std::string visible;
    for (auto const& line : frame)
    {
      visible += strip_sgr(line);
      visible += '\n';
    }
    visible_frames.push_back(std::move(visible));
  }
  auto const elapsed = std::chrono::steady_clock::now() - start;
  expect(frame.size() == 30, "tui very long transcript frame keeps the requested height");
  expect(std::ranges::all_of(frame, [](std::string const& line) { return line.find('\n') == std::string::npos && visible_columns(line) <= 96; }),
         "tui very long transcript frame keeps every rendered line inside the requested width");
  expect(visible_frames.size() == 3 && visible_frames[0] != visible_frames[1] && visible_frames[1] != visible_frames[2],
         "tui very long transcript stress validates that scroll offsets change visible transcript content");
  expect(elapsed < std::chrono::seconds(20), "tui very long transcript performance budget keeps full redraw viable for real-world scrollback scale");
}

}  // namespace

void run_tui_composer_tests()
{
  ScopedEnvVar no_color_guard("NO_COLOR", "");
  ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
  ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
  ScopedEnvVar tmux_hyperlinks_guard("AVA_TUI_TMUX_HYPERLINKS", "");
  test_tui_terminal_image_support();
  test_tui_composer_rendering_and_input();
  test_tui_session_grant_registry();
  test_tui_text_model_conversions();
  test_tui_event_state_reduces_runtime_events();
  test_ncurses_newterm_smoke_without_real_tty();
  test_tui_large_render_performance_budget();
  test_tui_large_tool_output_preview_is_bounded();
  test_tui_transcript_tail_renderer_matches_full_visible_window();
  test_tui_active_nonblocking_command_lane();
  test_tui_very_long_transcript_performance_budget();
}
