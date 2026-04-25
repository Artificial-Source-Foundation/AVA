#include "signal_cancel.hpp"

#include <csignal>
#include <stdexcept>

namespace ava::app {
namespace {

using SignalHandler = void (*)(int);

volatile std::sig_atomic_t g_headless_cancel_requested = 0;
SignalHandler g_previous_sigint = SIG_DFL;
SignalHandler g_previous_sigterm = SIG_DFL;
bool g_handlers_installed = false;
int g_handler_install_depth = 0;

void handle_headless_cancel_signal(int) {
  g_headless_cancel_requested = 1;
}

}  // namespace

void install_headless_signal_cancel_handlers() {
  if(g_handlers_installed) {
    ++g_handler_install_depth;
    return;
  }

  const auto previous_sigint = std::signal(SIGINT, handle_headless_cancel_signal);
  if(previous_sigint == SIG_ERR) {
    throw std::runtime_error("failed to install SIGINT cancellation handler");
  }

  const auto previous_sigterm = std::signal(SIGTERM, handle_headless_cancel_signal);
  if(previous_sigterm == SIG_ERR) {
    std::signal(SIGINT, previous_sigint);
    throw std::runtime_error("failed to install SIGTERM cancellation handler");
  }

  g_previous_sigint = previous_sigint;
  g_previous_sigterm = previous_sigterm;
  g_handlers_installed = true;
  g_handler_install_depth = 1;
}

void restore_headless_signal_cancel_handlers() {
  if(!g_handlers_installed) {
    return;
  }
  if(g_handler_install_depth > 1) {
    --g_handler_install_depth;
    return;
  }
  std::signal(SIGINT, g_previous_sigint);
  std::signal(SIGTERM, g_previous_sigterm);
  g_handlers_installed = false;
  g_handler_install_depth = 0;
}

void reset_headless_signal_cancel() {
  g_headless_cancel_requested = 0;
}

void request_headless_cancel_for_testing() {
  g_headless_cancel_requested = 1;
}

bool headless_signal_cancel_requested() {
  return g_headless_cancel_requested != 0;
}

}  // namespace ava::app
