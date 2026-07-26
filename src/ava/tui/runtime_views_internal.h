#pragma once

#include "ava/core/result.h"

#include <string>
#include <string_view>

namespace ava::agent {
struct QuestionAnswer;
struct QuestionPrompt;
}  // namespace ava::agent

namespace ava::permissions {
struct PermissionPrompt;
}  // namespace ava::permissions

namespace ava::tui {

struct ActiveRunHint;
struct PermissionPromptView;
struct QuestionPromptView;
struct TuiKeyBindings;

namespace runtime_views {

inline constexpr std::string_view kSettingsOpenKeybindings = "settings:keybindings.open";
inline constexpr std::string_view kSettingsValidateKeybindings = "settings:keybindings.validate";
inline constexpr std::string_view kSettingsEditKeybindings = "settings:keybindings.edit";
inline constexpr std::string_view kSettingsReloadKeybindings = "settings:keybindings.reload";
inline constexpr std::string_view kSettingsOpenModels = "settings:models.open";
inline constexpr std::string_view kSettingsOpenScopedModels = "settings:models.scoped";
inline constexpr std::string_view kSettingsTrustStatus = "settings:trust.status";
inline constexpr std::string_view kSettingsTrustProject = "settings:trust.project";
inline constexpr std::string_view kSettingsTrustDeny = "settings:trust.deny";
inline constexpr std::string_view kSettingsTrustClear = "settings:trust.clear";

[[nodiscard]] std::string permission_prompt_status(bool allow_session_available, bool allow_remember_available, bool deny_remember_available);
[[nodiscard]] ActiveRunHint active_run_hint_for(TuiKeyBindings const& bindings);
[[nodiscard]] std::string compact_path_leaf(std::string path);
[[nodiscard]] ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_view(QuestionPromptView const& prompt);
[[nodiscard]] PermissionPromptView permission_prompt_view(ava::permissions::PermissionPrompt const& prompt);
[[nodiscard]] QuestionPromptView question_prompt_view(ava::agent::QuestionPrompt const& prompt);

}  // namespace runtime_views

}  // namespace ava::tui
