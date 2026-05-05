#pragma once

#include <string_view>

namespace ava::app {

[[nodiscard]] bool open_url_in_browser(std::string_view url);

}  // namespace ava::app
