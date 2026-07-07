#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui::detail {

// Wrap terminal text by display columns while treating SGR escape sequences as
// zero-width style spans. Active SGR state is closed at synthetic line breaks
// and reopened on the next line so attributes do not bleed across rows.
[[nodiscard]] std::vector<std::string> wrap_ansi_text(std::string_view text, std::size_t width);

}  // namespace ava::tui::detail
