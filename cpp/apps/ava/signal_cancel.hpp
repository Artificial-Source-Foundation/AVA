#pragma once

namespace ava::app {

void install_headless_signal_cancel_handlers();
void restore_headless_signal_cancel_handlers();
void reset_headless_signal_cancel();
void request_headless_cancel_for_testing();
[[nodiscard]] bool headless_signal_cancel_requested();

}  // namespace ava::app
