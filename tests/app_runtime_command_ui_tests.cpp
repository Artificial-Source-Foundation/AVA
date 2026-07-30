#include "sys.h"
#include "tests/app_runtime_test_declarations.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/test_harness.h"
#include "ava/command/command.h"
#include "ava/app/commands.h"
#include "ava/app/display_settings.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/agent_loop.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/theme.h"
#include "ava/permissions/permission.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

void app_command_dispatcher_ui_part(ava::app::runtime::Session* session, ava::config::XdgPaths const& paths, std::filesystem::path const& workspace,
                                    std::vector<ava::app::CommandHotkey> const& custom_hotkeys)
{
  auto hotkeys = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/hotkeys", .hotkeys = custom_hotkeys});
  expect(hotkeys && hotkeys->handled && !hotkeys->output.empty() && hotkeys->output[0].find("Ctrl+M") != std::string::npos &&
             hotkeys->output[0].find("variant_cycle") != std::string::npos &&
             hotkeys->output[0].find("$XDG_CONFIG_HOME/ava/keybinds.json") != std::string::npos &&
             hotkeys->output[0].find("/reload keybindings") != std::string::npos,
         "command dispatcher /hotkeys reports effective keybind metadata");
  auto const default_hotkeys_text = ava::app::command_hotkeys_text({});
  auto const jump_line_pos = default_hotkeys_text.find("Jump to live tail");
  auto const jump_id_pos = default_hotkeys_text.find("jump_to_bottom");
  auto const mode_line_pos = default_hotkeys_text.find("Toggle build/plan mode");
  auto const mode_id_pos = default_hotkeys_text.find("mode_toggle");
  expect(jump_line_pos != std::string::npos && jump_id_pos != std::string::npos && jump_line_pos < jump_id_pos &&
             default_hotkeys_text.find("Ctrl+End", jump_line_pos) != std::string::npos && mode_line_pos != std::string::npos &&
             mode_id_pos != std::string::npos && mode_line_pos < mode_id_pos,
         "command help/hotkeys dense text leads with human labels and keeps machine ids secondary");
  auto packages_disabled = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/packages list"});
  expect(packages_disabled && packages_disabled->handled && !packages_disabled->output.empty() &&
             packages_disabled->output[0].find("/packages is disabled") != std::string::npos &&
             packages_disabled->output[0].find("provenance") != std::string::npos,
         "command dispatcher recognizes deferred package-manager commands instead of sending them to the model");
  auto keybindings = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings", .hotkeys = custom_hotkeys});
  expect(keybindings && keybindings->handled && !keybindings->output.empty() && keybindings->output[0].find("Keybindings:") != std::string::npos &&
             keybindings->output[0].find("Ctrl+M") != std::string::npos && keybindings->output[0].find("/keybindings init") != std::string::npos &&
             keybindings->output[0].find("/keybindings import <path>") != std::string::npos &&
             keybindings->output[0].find("/keybindings set <action>") != std::string::npos &&
             keybindings->output[0].find("/keybindings reset <action>") != std::string::npos &&
             keybindings->output[0].find("/keybindings validate") != std::string::npos,
         "command dispatcher /keybindings aliases the effective keybinding discovery surface");
  auto keybindings_validate_missing = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate"});
  expect(keybindings_validate_missing && keybindings_validate_missing->handled && !keybindings_validate_missing->output.empty() &&
             keybindings_validate_missing->output[0].find("No keybindings file found") != std::string::npos &&
             keybindings_validate_missing->output[0].find("/keybindings init") != std::string::npos,
         "command dispatcher /keybindings validate reports missing config without failing closed");
  auto keybindings_init = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init"});
  auto const keybinds_file = paths.ava_config_dir / "keybinds.json";
  auto const initialized_keybinds = ava::tui::load_key_bindings(keybinds_file);
  expect(keybindings_init && keybindings_init->handled && !keybindings_init->output.empty() &&
             keybindings_init->output[0].find("Created keybindings starter file") != std::string::npos &&
             keybindings_init->output[0].find(keybinds_file.string()) != std::string::npos && initialized_keybinds &&
             ava::tui::key_matches_action(*initialized_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Enter),
         "command dispatcher /keybindings init writes a validated starter file to the runtime config dir");
  auto keybindings_validate = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate"});
  expect(keybindings_validate && keybindings_validate->handled && !keybindings_validate->output.empty() &&
             keybindings_validate->output[0].find("keybindings file is valid") != std::string::npos &&
             keybindings_validate->output[0].find(keybinds_file.string()) != std::string::npos &&
             keybindings_validate->output[0].find("/reload keybindings") != std::string::npos,
         "command dispatcher /keybindings validate checks the configured keybind file without reloading");
  {
    std::ofstream output(keybinds_file, std::ios::binary | std::ios::trunc);
    output << "{\"submit\":\"Ctrl+P\",\"model_cycle_forward\":\"Ctrl+P\"}\n";
  }
  auto invalid_keybindings_validate = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate"});
  expect(invalid_keybindings_validate && invalid_keybindings_validate->handled && !invalid_keybindings_validate->output.empty() &&
             invalid_keybindings_validate->output[0].find("keybindings file is invalid") != std::string::npos &&
             invalid_keybindings_validate->output[0].find("conflicting TUI keybinding") != std::string::npos &&
             invalid_keybindings_validate->output[0].find("Ctrl+P") != std::string::npos &&
             invalid_keybindings_validate->output[0].find(keybinds_file.string()) != std::string::npos,
         "command dispatcher /keybindings validate surfaces parser diagnostics with path context");
  auto unsupported_keybindings_validate = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate --bad"});
  expect(unsupported_keybindings_validate && unsupported_keybindings_validate->handled && !unsupported_keybindings_validate->output.empty() &&
             unsupported_keybindings_validate->output[0].find("unsupported keybindings validate option") != std::string::npos,
         "command dispatcher /keybindings validate reports unsupported options");
  auto keybindings_init_existing = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init"});
  expect(keybindings_init_existing && keybindings_init_existing->handled && !keybindings_init_existing->output.empty() &&
             keybindings_init_existing->output[0].find("already exists") != std::string::npos &&
             keybindings_init_existing->output[0].find("--force") != std::string::npos,
         "command dispatcher /keybindings init refuses accidental overwrite");
  auto keybindings_init_force = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init --force"});
  expect(keybindings_init_force && keybindings_init_force->handled && !keybindings_init_force->output.empty() &&
             keybindings_init_force->output[0].find("Replaced keybindings starter file") != std::string::npos,
         "command dispatcher /keybindings init --force replaces the starter file explicitly");
  auto read_keybinds_file = [&] {
    std::ifstream input(keybinds_file, std::ios::binary);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
  };
  auto const import_source = workspace / "import-keybinds.json";
  auto const valid_import_content = std::string("{\"tui.editor.cursorLeft\":[\"Left\",\"Alt+H\"],\"app.tools.expand\":\"Ctrl+O\"}\n");
  write_app_test_file(import_source, valid_import_content);
  auto keybindings_import_existing = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings import import-keybinds.json"});
  expect(keybindings_import_existing && keybindings_import_existing->handled && !keybindings_import_existing->output.empty() &&
             keybindings_import_existing->output[0].find("keybindings file already exists") != std::string::npos &&
             keybindings_import_existing->output[0].find("/keybindings import <path> --force") != std::string::npos,
         "command dispatcher /keybindings import refuses accidental overwrite");
  auto keybindings_import_force = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings import import-keybinds.json --force"});
  auto const imported_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const installed_import_content = read_keybinds_file();
  expect(keybindings_import_force && keybindings_import_force->handled && !keybindings_import_force->output.empty() &&
             keybindings_import_force->output[0].find("Imported keybindings file") != std::string::npos &&
             keybindings_import_force->output[0].find(import_source.string()) != std::string::npos &&
             keybindings_import_force->output[0].find(keybinds_file.string()) != std::string::npos &&
             keybindings_import_force->output[0].find("/reload keybindings") != std::string::npos && installed_import_content == valid_import_content &&
             imported_keybinds && ava::tui::key_matches_action(*imported_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH) &&
             ava::tui::key_matches_action(*imported_keybinds, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlO),
         "command dispatcher /keybindings import --force validates and installs a relative source file");
  auto keybindings_set = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set cursor_left Alt+H"});
  auto const set_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const set_content = read_keybinds_file();
  expect(keybindings_set && keybindings_set->handled && !keybindings_set->output.empty() &&
             keybindings_set->output[0].find("Set keybinding") != std::string::npos &&
             keybindings_set->output[0].find("action: cursor_left") != std::string::npos &&
             keybindings_set->output[0].find("keys: Alt+H") != std::string::npos &&
             keybindings_set->output[0].find("/reload keybindings") != std::string::npos &&
             set_content.find("\"tui.editor.cursorLeft\": \"Alt+H\"") != std::string::npos && set_content.find("\"cursor_left\"") == std::string::npos &&
             set_keybinds && ava::tui::key_matches_action(*set_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH) &&
             !ava::tui::key_matches_action(*set_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::ArrowLeft),
         "command dispatcher /keybindings set validates, canonicalizes, and edits one action in keybinds.json");
  auto keybindings_set_multi = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set cursor_left Left,Alt+H"});
  auto const set_multi_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const set_multi_content = read_keybinds_file();
  expect(keybindings_set_multi && keybindings_set_multi->handled && !keybindings_set_multi->output.empty() &&
             keybindings_set_multi->output[0].find("keys: Left, Alt+H") != std::string::npos &&
             set_multi_content.find("\"tui.editor.cursorLeft\": [\"Left\", \"Alt+H\"]") != std::string::npos && set_multi_keybinds &&
             ava::tui::key_matches_action(*set_multi_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::ArrowLeft) &&
             ava::tui::key_matches_action(*set_multi_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH),
         "command dispatcher /keybindings set accepts comma-separated key lists");
  auto const before_failed_set_content = read_keybinds_file();
  auto keybindings_set_conflict = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set app.tools.expand Alt+H"});
  expect(keybindings_set_conflict && keybindings_set_conflict->handled && !keybindings_set_conflict->output.empty() &&
             keybindings_set_conflict->output[0].find("keybindings assignment is invalid") != std::string::npos &&
             keybindings_set_conflict->output[0].find("conflicting TUI keybinding") != std::string::npos &&
             keybindings_set_conflict->output[0].find("Target was not changed") != std::string::npos && read_keybinds_file() == before_failed_set_content,
         "command dispatcher /keybindings set validates the whole config before writing conflicts");
  auto keybindings_set_unknown_action = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set no_such_action Alt+H"});
  expect(keybindings_set_unknown_action && keybindings_set_unknown_action->handled && !keybindings_set_unknown_action->output.empty() &&
             keybindings_set_unknown_action->output[0].find("unknown TUI keybinding action") != std::string::npos,
         "command dispatcher /keybindings set reports unknown actions");
  auto keybindings_set_unknown_key = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set cursor_left Hyper+H"});
  expect(keybindings_set_unknown_key && keybindings_set_unknown_key->handled && !keybindings_set_unknown_key->output.empty() &&
             keybindings_set_unknown_key->output[0].find("unknown TUI key binding") != std::string::npos,
         "command dispatcher /keybindings set reports unknown keys");
  auto keybindings_reset = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings reset cursor_left"});
  auto const reset_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const reset_content = read_keybinds_file();
  expect(keybindings_reset && keybindings_reset->handled && !keybindings_reset->output.empty() &&
             keybindings_reset->output[0].find("Reset keybinding override") != std::string::npos &&
             keybindings_reset->output[0].find("action: cursor_left") != std::string::npos &&
             keybindings_reset->output[0].find("/reload keybindings") != std::string::npos &&
             reset_content.find("tui.editor.cursorLeft") == std::string::npos && reset_content.find("cursor_left") == std::string::npos && reset_keybinds &&
             ava::tui::key_matches_action(*reset_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::ArrowLeft) &&
             !ava::tui::key_matches_action(*reset_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH),
         "command dispatcher /keybindings reset removes equivalent action aliases and restores default bindings");
  auto keybindings_reset_missing = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings reset cursor_left"});
  expect(keybindings_reset_missing && keybindings_reset_missing->handled && !keybindings_reset_missing->output.empty() &&
             keybindings_reset_missing->output[0].find("No keybinding override found") != std::string::npos &&
             keybindings_reset_missing->output[0].find("Target was not changed") != std::string::npos,
         "command dispatcher /keybindings reset reports absent overrides without rewriting");
  auto keybindings_reset_unknown_action = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings reset no_such_action"});
  expect(keybindings_reset_unknown_action && keybindings_reset_unknown_action->handled && !keybindings_reset_unknown_action->output.empty() &&
             keybindings_reset_unknown_action->output[0].find("unknown TUI keybinding action") != std::string::npos,
         "command dispatcher /keybindings reset reports unknown actions");
  auto const invalid_import_source = workspace / "bad-keybinds.json";
  write_app_test_file(invalid_import_source, "{\"submit\":\"Ctrl+P\",\"model_cycle_forward\":\"Ctrl+P\"}\n");
  auto const before_invalid_import_content = read_keybinds_file();
  auto keybindings_import_invalid = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings import bad-keybinds.json --force"});
  expect(keybindings_import_invalid && keybindings_import_invalid->handled && !keybindings_import_invalid->output.empty() &&
             keybindings_import_invalid->output[0].find("keybindings import source is invalid") != std::string::npos &&
             keybindings_import_invalid->output[0].find("conflicting TUI keybinding") != std::string::npos &&
             keybindings_import_invalid->output[0].find("Target was not changed") != std::string::npos && read_keybinds_file() == before_invalid_import_content,
         "command dispatcher /keybindings import validates before replacing the target file");
  auto keybindings_import_missing = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings import missing-keybinds.json --force"});
  expect(keybindings_import_missing && keybindings_import_missing->handled && !keybindings_import_missing->output.empty() &&
             keybindings_import_missing->output[0].find("keybindings import source does not exist") != std::string::npos,
         "command dispatcher /keybindings import reports missing source files");
  auto unsupported_keybindings_import = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings import import-keybinds.json --bad"});
  expect(unsupported_keybindings_import && unsupported_keybindings_import->handled && !unsupported_keybindings_import->output.empty() &&
             unsupported_keybindings_import->output[0].find("unsupported keybindings import option") != std::string::npos,
         "command dispatcher /keybindings import reports unsupported options");
  auto unsupported_keybindings_init = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init --bad"});
  expect(unsupported_keybindings_init && unsupported_keybindings_init->handled && !unsupported_keybindings_init->output.empty() &&
             unsupported_keybindings_init->output[0].find("unsupported keybindings init option") != std::string::npos,
         "command dispatcher /keybindings init reports unsupported options");
  auto const symlink_keybinds_target = workspace / "symlink-target-keybinds.json";
  write_app_test_file(symlink_keybinds_target, valid_import_content);
  std::error_code remove_keybinds_error;
  std::filesystem::remove(keybinds_file, remove_keybinds_error);
  std::error_code keybinds_symlink_error;
  std::filesystem::create_symlink(symlink_keybinds_target, keybinds_file, keybinds_symlink_error);
  if (!keybinds_symlink_error)
  {
    auto keybindings_init_symlink = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init --force"});
    expect(keybindings_init_symlink && keybindings_init_symlink->handled && !keybindings_init_symlink->output.empty() &&
               keybindings_init_symlink->output[0].find("keybindings file target is a symlink") != std::string::npos,
           "command dispatcher /keybindings writes use atomic replacement and reject symlink targets");
    std::error_code cleanup_keybinds_symlink_error;
    std::filesystem::remove(keybinds_file, cleanup_keybinds_symlink_error);
  }
  auto keybindings_restore_after_symlink_test = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init --force"});
  expect(keybindings_restore_after_symlink_test && keybindings_restore_after_symlink_test->handled && !keybindings_restore_after_symlink_test->output.empty() &&
             keybindings_restore_after_symlink_test->output[0].find("Replaced keybindings starter file") != std::string::npos,
         "command dispatcher /keybindings init restores a normal config file after symlink safety checks");
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto theme_status = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme"});
    expect(theme_status && theme_status->handled && !theme_status->output.empty() && theme_status->output[0].find("TUI theme:") != std::string::npos &&
               theme_status->output[0].find("usage: /theme [dark|light|plain|custom-name|reset]") != std::string::npos,
           "command dispatcher /theme reports current config, active theme, and usage");
    auto theme_light = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme light"});
    auto loaded_theme = ava::app::load_tui_display_settings(paths);
    auto active_theme = ava::tui::active_tui_theme();
    expect(theme_light && theme_light->handled && !theme_light->output.empty() && theme_light->output[0].find("Stored TUI theme light") != std::string::npos &&
               loaded_theme && loaded_theme->theme && *loaded_theme->theme == "light" && active_theme.kind == ava::tui::TuiThemeKind::Light &&
               active_theme.badge == "display.json",
           "command dispatcher /theme light persists display.json and applies the active TUI theme override");
    write_app_test_file(paths.ava_config_dir / "themes" / "sunrise.json",
                        "{\n"
                        "  \"name\": \"sunrise\",\n"
                        "  \"vars\": {\"primary\": \"#0066cc\", \"paper\": 255},\n"
                        "  \"colors\": {\n"
                        "    \"text\": \"\",\n"
                        "    \"muted\": 242,\n"
                        "    \"success\": 34,\n"
                        "    \"warning\": \"#ffaa00\",\n"
                        "    \"error\": \"#ff0000\",\n"
                        "    \"accent\": \"primary\",\n"
                        "    \"screenBg\": \"paper\",\n"
                        "    \"composerBg\": 236,\n"
                        "    \"toolBg\": 235,\n"
                        "    \"questionBg\": 234\n"
                        "  }\n"
                        "}\n");
    write_app_test_file(
        paths.ava_config_dir / "themes" / "legacy.json",
        "{\n"
        "  \"name\": \"legacy\",\n"
        "  \"colors\": {\"text\":\"\",\"muted\":242,\"success\":34,\"warning\":220,\"error\":196,\"accent\":39,\"screenBg\":235,\"composerBg\":236}\n"
        "}\n");
    write_app_test_file(paths.ava_config_dir / "themes" / "broken.json",
                        "{\n"
                        "  \"name\": \"broken\",\n"
                        "  \"colors\": {\"text\":\"\",\"muted\":242,\"success\":34,\"warning\":220,\"error\":196,\"accent\":39,\"screenBg\":235}\n"
                        "}\n");
    auto custom_theme = ava::app::load_tui_custom_theme(paths, "sunrise");
    auto legacy_theme = ava::app::load_tui_custom_theme(paths, "legacy");
    auto invalid_custom_theme_file = ava::app::load_tui_custom_theme_file(paths.ava_config_dir / "themes" / "broken.json");
    expect(custom_theme && custom_theme->name == "sunrise" && custom_theme->palette.text == -1 && custom_theme->palette.screen_bg == 255 &&
               custom_theme->palette.composer_bg == 236 && custom_theme->palette.tool_bg == 235 && custom_theme->palette.question_bg == 234 && legacy_theme &&
               legacy_theme->palette.tool_bg == legacy_theme->palette.composer_bg && legacy_theme->palette.question_bg == legacy_theme->palette.composer_bg &&
               !invalid_custom_theme_file && invalid_custom_theme_file.error().format().find("composerBg") != std::string::npos,
           "display settings load custom tool/question backgrounds, preserve composer fallbacks, and reject incomplete custom themes with token context");
    auto theme_custom = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme sunrise"});
    loaded_theme = ava::app::load_tui_display_settings(paths);
    active_theme = ava::tui::active_tui_theme();
    expect(theme_custom && theme_custom->handled && !theme_custom->output.empty() &&
               theme_custom->output[0].find("Stored TUI theme sunrise") != std::string::npos && loaded_theme && loaded_theme->theme &&
               *loaded_theme->theme == "sunrise" && loaded_theme->custom_theme && active_theme.kind == ava::tui::TuiThemeKind::Custom &&
               active_theme.name == "sunrise" && active_theme.badge == "display.json" && active_theme.palette && active_theme.palette->composer_bg == 236 &&
               active_theme.palette->tool_bg == 235 && active_theme.palette->question_bg == 234,
           "command dispatcher /theme <custom> persists display.json and applies a valid custom TUI theme");
    auto custom_watch = ava::app::load_tui_display_settings_watch_state(paths);
    expect(custom_watch && custom_watch->theme && *custom_watch->theme == "sunrise" && custom_watch->custom_theme_revision,
           "display settings watch state records the selected custom theme revision");
    write_app_test_file(paths.ava_config_dir / "themes" / "sunrise.json",
                        "{\n"
                        "  \"name\": \"sunrise\",\n"
                        "  \"vars\": {\"primary\": \"#0066cc\", \"paper\": 255},\n"
                        "  \"colors\": {\n"
                        "    \"text\": \"\",\n"
                        "    \"muted\": 242,\n"
                        "    \"success\": 34,\n"
                        "    \"warning\": \"#ffaa00\",\n"
                        "    \"error\": \"#ff0000\",\n"
                        "    \"accent\": \"primary\",\n"
                        "    \"screenBg\": \"paper\",\n"
                        "    \"composerBg\": 237\n"
                        "  }\n"
                        "}\n");
    auto changed_custom_watch = ava::app::load_tui_display_settings_watch_state(paths);
    expect(custom_watch && changed_custom_watch && ava::app::tui_display_settings_watch_state_changed(*custom_watch, *changed_custom_watch) &&
               changed_custom_watch->custom_theme_revision != custom_watch->custom_theme_revision,
           "display settings watch state detects selected custom theme file edits by content revision");
    write_app_test_file(paths.ava_config_dir / "display.json", "{\n  \"theme\": \"plain\"\n}\n");
    auto changed_display_watch = ava::app::load_tui_display_settings_watch_state(paths);
    expect(changed_custom_watch && changed_display_watch && ava::app::tui_display_settings_watch_state_changed(*changed_custom_watch, *changed_display_watch) &&
               changed_display_watch->theme && *changed_display_watch->theme == "plain" && !changed_display_watch->custom_theme_revision,
           "display settings watch state detects display.json edits and clears custom theme tracking");
    auto invalid_theme = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme sepia"});
    expect(invalid_theme && invalid_theme->handled && !invalid_theme->output.empty() &&
               invalid_theme->output[0].find("unsupported theme: sepia") != std::string::npos &&
               invalid_theme->output[0].find("dark|light|plain|custom-name|reset") != std::string::npos,
           "command dispatcher /theme rejects unsupported theme names with usage");
    auto reset_theme = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme reset"});
    auto reset_loaded_theme = ava::app::load_tui_display_settings(paths);
    active_theme = ava::tui::active_tui_theme();
    expect(reset_theme && reset_theme->handled && !reset_theme->output.empty() && reset_theme->output[0].find("Reset TUI theme") != std::string::npos &&
               reset_loaded_theme && !reset_loaded_theme->theme && active_theme.kind == ava::tui::TuiThemeKind::Dark && active_theme.badge == "built-in",
           "command dispatcher /theme reset clears the persisted TUI theme and returns to the built-in default");
  }
  auto details = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/details"});
  expect(details && details->handled && !details->output.empty() && details->output[0].find("default to Rich") != std::string::npos &&
             details->output[0].find("/details compact") != std::string::npos,
         "command dispatcher documents Rich default and explicit tool-card presentation modes");
  auto sidebar = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sidebar"});
  expect(sidebar && sidebar->handled && !sidebar->output.empty() && sidebar->output[0].find("interactive TUI") != std::string::npos &&
             sidebar->output[0].find("session overview") != std::string::npos && sidebar->output[0].find("session_id") == std::string::npos &&
             sidebar->output[0].find("workspace") == std::string::npos,
         "command dispatcher describes /sidebar as a TUI-only view without inventing sidebar data");
  auto search = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/search Unicode literal"});
  expect(search && search->handled && !search->output.empty() && search->output[0].find("only inside the interactive TUI") != std::string::npos &&
             search->output[0].find("currently rendered transcript items") != std::string::npos,
         "headless command dispatcher truthfully identifies transcript search as a TUI-only rendered view");
  auto tool = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/tools write"});
  expect(tool && tool->handled && !tool->output.empty() && tool->output[0].find("/tool [query] to toggle the latest or matching card") != std::string::npos &&
             tool->output[0].find("inherited non-expanded view and Expanded") != std::string::npos &&
             tool->output[0].find("/copy tool [query] to copy safe tool details") != std::string::npos,
         "command dispatcher recognizes /tool and /tools as TUI tool-card inspection commands");
  auto diff = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/diff src/main.cpp"});
  expect(diff && diff->handled && !diff->output.empty() &&
             diff->output[0].find("/diff [query] to show the latest or matching unified diff") != std::string::npos &&
             diff->output[0].find("/copy diff [query] to copy it") != std::string::npos,
         "command dispatcher recognizes filtered /diff as a TUI transcript inspection command");
  auto copy = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy tool"});
  expect(copy && copy->handled && !copy->output.empty() && copy->output[0].find("/copy for the latest AVA message") != std::string::npos &&
             copy->output[0].find("/copy user [query] to pick a public user turn") != std::string::npos &&
             copy->output[0].find("/copy tool [query] for tool details") != std::string::npos &&
             copy->output[0].find("/copy diff [query] for unified diffs") != std::string::npos &&
             copy->output[0].find("/copy permission [query] for permission audit details") != std::string::npos,
         "command dispatcher recognizes /copy as a TUI clipboard command");
  auto copy_user = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy user"});
  expect(copy_user && copy_user->handled && !copy_user->output.empty() &&
             copy_user->output[0].find("/copy user [query] to pick a public user turn") != std::string::npos,
         "command dispatcher recognizes exact /copy user as a TUI user-turn clipboard target");
  auto copy_diff = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy diff src/main.cpp"});
  expect(
      copy_diff && copy_diff->handled && !copy_diff->output.empty() && copy_diff->output[0].find("/copy diff [query] for unified diffs") != std::string::npos,
      "command dispatcher recognizes filtered /copy diff as a TUI clipboard command");
  auto copy_permission = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy permission git push"});
  expect(copy_permission && copy_permission->handled && !copy_permission->output.empty() &&
             copy_permission->output[0].find("/copy permission [query] for permission audit details") != std::string::npos,
         "command dispatcher recognizes filtered /copy permission as a TUI clipboard command");
  auto unsupported_copy = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy branch"});
  expect(unsupported_copy && unsupported_copy->handled && !unsupported_copy->output.empty() &&
             unsupported_copy->output[0].find("unsupported copy target: branch") != std::string::npos &&
             unsupported_copy->output[0].find("supported: user [query], tool [query], diff [query], permission [query]") != std::string::npos,
         "command dispatcher reports unsupported copy targets");
  auto fork_from = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/fork-from"});
  expect(fork_from && fork_from->handled && !fork_from->output.empty() && fork_from->output[0].find("interactive TUI") != std::string::npos &&
             fork_from->output[0].find("/fork-from") != std::string::npos && fork_from->output[0].find("/fork [name]") != std::string::npos,
         "command dispatcher recognizes /fork-from as a TUI user-turn fork picker");
  auto thinking = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/thinking"});
  expect(thinking && thinking->handled && !thinking->output.empty() && thinking->output[0].find("TUI display toggle") != std::string::npos &&
             thinking->output[0].find("does not change provider reasoning mode") != std::string::npos &&
             thinking->output[0].find("/thinking details") != std::string::npos,
         "command dispatcher recognizes /thinking as display-only instead of changing backend reasoning mode");
  auto thinking_details = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/thinking details"});
  expect(
      thinking_details && thinking_details->handled && !thinking_details->output.empty() && thinking_details->output[0].find("Thinking:") != std::string::npos,
      "command dispatcher documents /thinking details as the per-item expand fallback");
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    write_app_test_file(paths.ava_config_dir / "display.json", "{\n  \"theme\": \"plain\"\n}\n");
    auto reload_theme = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload theme"});
    auto active_theme = ava::tui::active_tui_theme();
    expect(reload_theme && reload_theme->handled && !reload_theme->output.empty() && reload_theme->output[0].find("Reload report:") != std::string::npos &&
               reload_theme->output[0].find("display: loaded") != std::string::npos && reload_theme->output[0].find("configured: plain") != std::string::npos &&
               active_theme.kind == ava::tui::TuiThemeKind::Plain && active_theme.badge == "display.json",
           "command dispatcher /reload theme applies externally edited display.json without restarting");
    ava::tui::set_tui_config_theme(std::nullopt);
  }
  auto unsupported_reload = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload no-such"});
  expect(unsupported_reload && unsupported_reload->handled && !unsupported_reload->output.empty() &&
             unsupported_reload->output[0].find("unsupported reload target: no-such") != std::string::npos &&
             unsupported_reload->output[0].find(
                 "supported: all, theme, models, prompts, trust, compaction, keybindings, auth, permissions, lsp, mcp, plugins") != std::string::npos,
         "command dispatcher reports unsupported reload targets");
  auto help = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/help", .hotkeys = custom_hotkeys});
  expect(help && help->handled && !help->output.empty() && help->output[0].find("/hotkeys") != std::string::npos &&
             help->output[0].find("/keybindings") != std::string::npos && help->output[0].find("/sidebar") != std::string::npos &&
             help->output[0].find("/search") != std::string::npos && help->output[0].find("/connect") != std::string::npos &&
             help->output[0].find("/plugins") != std::string::npos && help->output[0].find("!<command>") != std::string::npos &&
             help->output[0].find("!!<command>") != std::string::npos && help->output[0].find("Unavailable commands") != std::string::npos &&
             help->output[0].find("Ctrl+M") != std::string::npos,
         "command dispatcher /help includes catalog commands and effective hotkeys");

  int shell_prompts = 0;
  auto const shell_resolver =
      [&shell_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    ++shell_prompts;
    expect(prompt.command_metadata && prompt.command_metadata->level == ava::command::CommandLevel::Critical &&
               prompt.command_metadata->family == ava::command::CommandFamily::RawShell &&
               prompt.command_metadata->execution_domain == ava::command::CommandExecutionDomain::RawShell &&
               prompt.command_metadata->backend_maximum_scope == ava::command::InteractiveScope::Once &&
               !ava::permissions::command_permission_allows_reusable_grant(*prompt.command_metadata),
           "Pi-style shell helper receives a critical one-shot raw-shell prompt");
    return ava::permissions::PermissionResolution::Allow;
  };
  auto bang_shell = ava::app::run_command(*session, ava::app::CommandRequest{.command = "!pwd", .permission_resolver = shell_resolver});
  expect(bang_shell && bang_shell->handled && bang_shell->tool_timeline.size() == 2 && bang_shell->tool_timeline[0].name == "bash" &&
             bang_shell->tool_timeline[0].argument_summary == "<redacted one-shot command>" &&
             bang_shell->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success && !bang_shell->output.empty() &&
             bang_shell->output[0].find("exit: 0") != std::string::npos,
         "Pi-style ! shell helper runs through the permissioned bash command path");
  auto hidden_bang_shell = ava::app::run_command(*session, ava::app::CommandRequest{.command = "!! pwd", .permission_resolver = shell_resolver});
  expect(hidden_bang_shell && hidden_bang_shell->handled && hidden_bang_shell->tool_timeline.size() == 2 &&
             hidden_bang_shell->tool_timeline[0].name == "bash" && hidden_bang_shell->tool_timeline[0].argument_summary == "<redacted one-shot command>" &&
             hidden_bang_shell->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success && shell_prompts == 2,
         "Pi-style !! shell helper is accepted as the critical one-shot bash helper without bypassing permissions");
  std::optional<ava::permissions::PermissionPrompt> explicit_bash_prompt;
  auto explicit_bash =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/bash git status",
                                                               .permission_resolver = [&explicit_bash_prompt](ava::permissions::PermissionPrompt const& prompt)
                                                                   -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
                                                                 explicit_bash_prompt = prompt;
                                                                 return ava::permissions::PermissionResolution::Deny;
                                                               }});
  expect(explicit_bash && explicit_bash->handled && explicit_bash_prompt && explicit_bash_prompt->command_metadata &&
             explicit_bash_prompt->command_metadata->level == ava::command::CommandLevel::Critical &&
             explicit_bash_prompt->command_metadata->family == ava::command::CommandFamily::RawShell &&
             explicit_bash_prompt->command_metadata->execution_domain == ava::command::CommandExecutionDomain::RawShell &&
             explicit_bash_prompt->command_metadata->backend_maximum_scope == ava::command::InteractiveScope::Once &&
             !ava::permissions::command_permission_allows_reusable_grant(*explicit_bash_prompt->command_metadata) && !explicit_bash->tool_timeline.empty() &&
             explicit_bash->tool_timeline.back().status == ava::agent::ToolTimelineStatus::Error,
         "/bash git status is an explicit Critical raw-shell prompt and cannot create a reusable grant");
  auto missing_bang_shell = ava::app::run_command(*session, ava::app::CommandRequest{.command = "!"});
  expect(missing_bang_shell && missing_bang_shell->handled && !missing_bang_shell->output.empty() &&
             missing_bang_shell->output[0].find("!<command> or !!<command>") != std::string::npos,
         "empty shell helper reports the expected usage");
  auto find_alias = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/find src/*.cpp"});
  expect(find_alias && find_alias->handled && find_alias->tool_timeline.size() == 2 && find_alias->tool_timeline[0].name == "find" &&
             find_alias->tool_timeline[1].result_json.find("\"tool\":\"glob\"") != std::string::npos && !find_alias->output.empty() &&
             find_alias->output[0].find("src/main.cpp") != std::string::npos,
         "Pi-style /find alias runs through the native glob tool path");
  auto ls_alias = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/ls src"});
  expect(ls_alias && ls_alias->handled && ls_alias->tool_timeline.size() == 2 && ls_alias->tool_timeline[0].name == "ls" &&
             ls_alias->tool_timeline[1].result_json.find("\"tool\":\"list_directory\"") != std::string::npos && !ls_alias->output.empty() &&
             ls_alias->output[0].find("main.cpp") != std::string::npos,
         "Pi-style /ls alias runs through the native list_directory tool path");
  auto missing_find_alias = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/find"});
  expect(missing_find_alias && missing_find_alias->handled && !missing_find_alias->output.empty() &&
             missing_find_alias->output[0].find("/find <pattern>") != std::string::npos,
         "empty /find alias reports Pi-style usage instead of the native /glob name");

  auto plugins_usage = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins"});
  expect(plugins_usage && plugins_usage->handled && !plugins_usage->output.empty() && plugins_usage->output[0].find("usage: /plugins") != std::string::npos,
         "command dispatcher /plugins without a subcommand reports usage");
  auto plugins = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins && plugins->handled && !plugins->output.empty() && plugins->output[0].find("com.example.global") != std::string::npos &&
             plugins->output[0].find("com.example.project") != std::string::npos && plugins->output[0].find("Failures: 1") != std::string::npos,
         "command dispatcher /plugins list reports discovered plugins and diagnostics failures");
  auto inspect_plugin = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins inspect com.example.project"});
  expect(inspect_plugin && inspect_plugin->handled && !inspect_plugin->output.empty() &&
             inspect_plugin->output[0].find("entrypoint: node plugin.js --safe (not executed)") != std::string::npos &&
             inspect_plugin->output[0].find("no plugin process is started yet") != std::string::npos,
         "command dispatcher /plugins inspect shows manifest details without executing entrypoints");
  auto enable_plugin = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins enable com.example.project"});
  expect(enable_plugin && enable_plugin->handled && !enable_plugin->output.empty() &&
             enable_plugin->output[0].find("Enabled project plugin com.example.project") != std::string::npos &&
             enable_plugin->output[0].find("No plugin process was started") != std::string::npos,
         "command dispatcher /plugins enable records state without starting plugin processes");
  auto plugins_after_enable = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins_after_enable && plugins_after_enable->handled && !plugins_after_enable->output.empty() &&
             plugins_after_enable->output[0].find("com.example.project  enabled") != std::string::npos,
         "command dispatcher /plugins list reflects enablement state");
}

}  // namespace ava::tests::app_runtime_tests
