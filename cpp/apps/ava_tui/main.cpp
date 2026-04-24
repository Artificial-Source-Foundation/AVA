#include <atomic>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "ava/agent/agent.hpp"
#include "ava/config/paths.hpp"
#include "ava/orchestration/composition.hpp"
#include "options.hpp"
#include "state.hpp"

#if AVA_WITH_FTXUI
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#endif

namespace {

#if AVA_WITH_FTXUI
[[nodiscard]] std::string interactive_request_label(
    const std::optional<ava::control_plane::InteractiveRequestHandle>& request
) {
  if(!request.has_value()) {
    return "-";
  }
  if(request->request_id.empty()) {
    return "-";
  }
  return request->request_id;
}
#endif

#if AVA_WITH_FTXUI
class TuiApp {
  public:
   explicit TuiApp(ava::tui::TuiOptions options)
      : options_(std::move(options)),
        composition_(ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
            .session_db_path = ava::config::app_db_path(),
            .workspace_root = std::filesystem::current_path(),
            .resume_latest = options_.resume,
            .session_id = options_.session_id,
            .selection = ava::orchestration::RuntimeSelectionOptions{
                .provider = options_.provider,
                .model = options_.model,
                .max_turns = options_.max_turns,
                .max_turns_explicit = options_.max_turns_explicit,
            },
            .auto_approve = options_.auto_approve,
            .allowed_tools = std::nullopt,
            .system_prompt_preamble = std::nullopt,
            .provider_override = nullptr,
            .provider_factory = nullptr,
            .credentials_override = std::nullopt,
        })) {

    const auto intro = fmt::format(
        "session={} provider={} model={}{}",
        composition_.session.id,
        composition_.selection.provider,
        composition_.selection.model,
        options_.auto_approve ? " auto_approve=on" : ""
    );
    state_.set_model_identity(composition_.selection.provider, composition_.selection.model);
    state_.set_status_line(intro);
  }

  ~TuiApp() {
    join_run_thread();
  }

  int run() {
    using namespace ftxui;

    ScreenInteractive screen = ScreenInteractive::Fullscreen();
    screen_.store(&screen, std::memory_order_release);
    struct ScreenPointerReset final {
      std::atomic<ftxui::ScreenInteractive*>& pointer;
      ~ScreenPointerReset() {
        pointer.store(nullptr, std::memory_order_release);
      }
    } screen_pointer_reset{screen_};

    auto renderer = Renderer([&] {
      std::lock_guard lock(state_mutex_);
      const auto rows = static_cast<std::size_t>(std::max(1, screen.dimy() - 10));
      state_.set_viewport_rows(rows);
      sync_interactive_state_locked();

      std::vector<Element> message_rows;
      const auto visible = state_.visible_messages();
      if(visible.empty()) {
        message_rows.push_back(text("No messages yet."));
      } else {
        for(const auto& line : visible) {
          std::string prefix;
          switch(line.kind) {
            case ava::tui::MessageKind::User:
              prefix = "[user] ";
              break;
            case ava::tui::MessageKind::Assistant:
              prefix = "[assistant] ";
              break;
            case ava::tui::MessageKind::System:
              prefix = "[system] ";
              break;
            case ava::tui::MessageKind::Error:
              prefix = "[error] ";
              break;
          }
          message_rows.push_back(text(prefix + line.text));
        }
      }

      const auto running = state_.running();
      const auto composer_label = running ? "Composer (running)" : "Composer";
      const auto status = state_.status_line();
      const auto input = state_.input_buffer();
      const auto navigation_line = state_.message_navigation_line();
      const auto requests = state_.interactive_requests();
      const auto interactive_line = fmt::format(
          "Interactive pending: total={} approval={} question={} plan={}",
          requests.pending_count(),
          interactive_request_label(requests.approval),
          interactive_request_label(requests.question),
          interactive_request_label(requests.plan)
      );

      return vbox({
                  window(text("Messages"), vbox(std::move(message_rows)) | frame | vscroll_indicator) | flex,
                  separator(),
                  window(text(composer_label), text(input.empty() ? "" : input)),
                  separator(),
                  text(navigation_line),
                  text(interactive_line),
                  text(status),
                  text("Enter=submit  Up/Down=history  PgUp/PgDn/Home/End=messages  q=quit"),
              }) |
              border;
    });

    auto component = CatchEvent(renderer, [&](const ftxui::Event& event) {
      if(event == ftxui::Event::Custom) {
        std::lock_guard lock(state_mutex_);
        if(state_.quit_requested() && !state_.running()) {
          screen.ExitLoopClosure()();
          return true;
        }
      }

      if(event.is_character()) {
        const auto character = event.character();
        if(character == "q") {
          std::lock_guard lock(state_mutex_);
          if(!state_.input_buffer().empty()) {
            state_.insert_text(character);
            return true;
          }
          if(state_.running()) {
            quit_when_run_finishes_.store(true);
            if(const auto cancel_handle = current_cancel_handle(); cancel_handle.has_value()) {
              cancel_handle->cancel();
              state_.set_status_line("Run cancellation requested. Waiting for cooperative stop...");
            } else {
              state_.set_status_line("Run in progress. Will quit when this run finishes.");
            }
          } else {
            state_.request_quit();
            screen.ExitLoopClosure()();
          }
          return true;
        }

        std::lock_guard lock(state_mutex_);
        state_.insert_text(character);
        return true;
      }

      if(event == ftxui::Event::Backspace) {
        std::lock_guard lock(state_mutex_);
        state_.backspace();
        return true;
      }

      if(event == ftxui::Event::ArrowUp) {
        std::lock_guard lock(state_mutex_);
        if(!state_.history_previous()) {
          state_.scroll_up(1);
        }
        return true;
      }
      if(event == ftxui::Event::ArrowDown) {
        std::lock_guard lock(state_mutex_);
        if(!state_.history_next()) {
          state_.scroll_down(1);
        }
        return true;
      }
      if(event == ftxui::Event::PageUp) {
        std::lock_guard lock(state_mutex_);
        state_.page_up();
        return true;
      }
      if(event == ftxui::Event::PageDown) {
        std::lock_guard lock(state_mutex_);
        state_.page_down();
        return true;
      }
      if(event == ftxui::Event::Home) {
        std::lock_guard lock(state_mutex_);
        state_.scroll_to_top();
        return true;
      }
      if(event == ftxui::Event::End) {
        std::lock_guard lock(state_mutex_);
        state_.scroll_to_bottom();
        return true;
      }

      if(event == ftxui::Event::Return) {
        submit_from_ui();
        return true;
      }

      return false;
    });

    try {
      screen.Loop(component);
      join_run_thread();
      composition_.save_session();
    } catch(...) {
      join_run_thread();
      try {
        composition_.save_session();
      } catch(...) {
        // Preserve the original UI/runtime exception; save-on-error is best-effort.
      }
      throw;
    }
    return 0;
  }

  private:
  void join_run_thread() {
    if(!run_thread_.joinable()) {
      return;
    }
    run_thread_.join();
  }

  void post_custom_event() {
    if(auto* screen = screen_.load(std::memory_order_acquire); screen != nullptr) {
      screen->PostEvent(ftxui::Event::Custom);
    }
  }

  void submit_from_ui() {
    std::optional<std::string> prompt;
    {
      std::lock_guard lock(state_mutex_);
      prompt = state_.take_submission();
    }
    if(!prompt.has_value()) {
      return;
    }

    join_run_thread();
    quit_when_run_finishes_.store(false);

    run_thread_ = std::thread([this, prompt = *prompt] {
      const auto run_lease = composition_.run_controller->begin_run();
      composition_.interactive_bridge->set_run_id(run_lease.run_id);
      set_current_cancel_handle(run_lease.handle);

      const auto result = composition_.runtime->run(
          composition_.session,
          ava::agent::AgentRunInput{
              .goal = prompt,
              .queue = &composition_.queue,
              .run_id = run_lease.run_id,
              .is_cancelled = [&] {
                return run_lease.token.is_cancelled();
              },
              .stream = true,
          },
          [&](const ava::agent::AgentEvent& event) {
            {
              std::lock_guard lock(state_mutex_);
              state_.apply_agent_event(event);
              sync_interactive_state_locked();
            }
            post_custom_event();
          }
      );

      {
        std::lock_guard lock(state_mutex_);
        if(result.error.has_value()) {
          state_.set_running(false);
          state_.set_status_line("Run failed: " + *result.error);
        }
      }

      composition_.interactive_bridge->set_run_id(std::nullopt);
      clear_current_cancel_handle();

      post_custom_event();

      if(quit_when_run_finishes_.load()) {
        {
          std::lock_guard lock(state_mutex_);
          state_.request_quit();
        }
        if(auto* screen = screen_.load(std::memory_order_acquire); screen != nullptr) {
          screen->PostEvent(ftxui::Event::Custom);
        }
      }
    });
  }

  [[nodiscard]] std::optional<ava::orchestration::RunCancellationHandle> current_cancel_handle() const {
    const std::lock_guard<std::mutex> lock(run_control_mutex_);
    return current_cancel_handle_;
  }

  void set_current_cancel_handle(ava::orchestration::RunCancellationHandle handle) {
    const std::lock_guard<std::mutex> lock(run_control_mutex_);
    current_cancel_handle_ = std::move(handle);
  }

  void clear_current_cancel_handle() {
    const std::lock_guard<std::mutex> lock(run_control_mutex_);
    current_cancel_handle_.reset();
  }

  void sync_interactive_state_locked() {
    state_.set_interactive_request(
        ava::control_plane::InteractiveRequestKind::Approval,
        composition_.interactive_bridge->approval_requests().current_pending()
    );
    state_.set_interactive_request(
        ava::control_plane::InteractiveRequestKind::Question,
        composition_.interactive_bridge->question_requests().current_pending()
    );
    state_.set_interactive_request(
        ava::control_plane::InteractiveRequestKind::Plan,
        composition_.interactive_bridge->plan_requests().current_pending()
    );
  }

  ava::tui::TuiOptions options_;
  ava::orchestration::RuntimeComposition composition_;

  ava::tui::AppState state_;
  std::mutex state_mutex_;
  mutable std::mutex run_control_mutex_;
  std::thread run_thread_;
  std::optional<ava::orchestration::RunCancellationHandle> current_cancel_handle_;
  std::atomic<bool> quit_when_run_finishes_{false};

  std::atomic<ftxui::ScreenInteractive*> screen_{nullptr};
};
#endif

}  // namespace

int main(int argc, char** argv) {
  ava::tui::TuiOptions options;
  try {
    options = ava::tui::parse_tui_options_or_throw(argc, argv);
  } catch(const CLI::CallForHelp& e) {
    fmt::print("{}\n", e.what());
    return 0;
  } catch(const std::exception& ex) {
    fmt::print(stderr, "error: {}\n", ex.what());
    return 2;
  }

#if AVA_WITH_FTXUI
  try {
    TuiApp app(std::move(options));
    return app.run();
  } catch(const std::exception& ex) {
    fmt::print(stderr, "error: {}\n", ex.what());
    return 2;
  }
#else
  (void) options;
  fmt::print(
      stderr,
      "error: ava_tui requires FTXUI. Reconfigure with -DAVA_WITH_FTXUI=ON and install ftxui.\n"
  );
  return 2;
#endif
}
