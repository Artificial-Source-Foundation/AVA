#include <atomic>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "agent_config.hpp"
#include "ava/agent/agent.hpp"
#include "ava/config/paths.hpp"
#include "ava/session/session.hpp"
#include "ava/tools/tools.hpp"
#include "session_resolver.hpp"
#include "state.hpp"

#if AVA_WITH_FTXUI
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#endif

namespace {

struct TuiOptions {
  std::optional<std::string> provider;
  std::optional<std::string> model;
  bool resume{false};
  std::optional<std::string> session_id;
  std::size_t max_turns{16};
  bool max_turns_explicit{false};
  bool auto_approve{false};
};

class AllowAllApprovalBridge final : public ava::tools::ApprovalBridge {
 public:
  [[nodiscard]] ava::tools::ToolApproval request_approval(
      const ava::types::ToolCall&,
      const ava::tools::PermissionInspection&
  ) const override {
    return ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed};
  }
};

TuiOptions parse_tui_options_or_throw(int argc, char** argv) {
  TuiOptions options;

  CLI::App app{"AVA C++ Milestone 12 bounded FTXUI TUI"};
  app.add_option("--provider", options.provider, "Provider override");
  app.add_option("--model", options.model, "Model override");
  app.add_flag("-c,--continue", options.resume, "Continue latest session");
  app.add_option("--session", options.session_id, "Continue a specific session id");
  auto* max_turns = app.add_option("--max-turns", options.max_turns, "Maximum runtime turns");
  max_turns->check(CLI::Range(1, 10000));
  app.add_flag("--auto-approve", options.auto_approve, "Allow tool approvals without interaction");

  try {
    app.parse(argc, argv);
  } catch(const CLI::CallForHelp&) {
    throw;
  } catch(const CLI::ParseError&) {
    throw std::invalid_argument(std::string("invalid CLI arguments: ") + app.help());
  }

  options.max_turns_explicit = max_turns->count() > 0;
  if(options.resume && options.session_id.has_value()) {
    throw std::invalid_argument("--continue and --session cannot be used together");
  }

  return options;
}

#if AVA_WITH_FTXUI
class TuiApp : public std::enable_shared_from_this<TuiApp> {
 public:
  explicit TuiApp(TuiOptions options)
      : options_(std::move(options)),
        sessions_(ava::config::app_db_path()) {
    auto startup = ava::app::resolve_startup_session(sessions_, options_.resume, options_.session_id);
    session_ = std::move(startup.session);

    ava::app::CliOptions headless_like;
    headless_like.provider = options_.provider;
    headless_like.model = options_.model;
    headless_like.max_turns = options_.max_turns;
    headless_like.max_turns_explicit = options_.max_turns_explicit;
    selection_ = ava::app::resolve_agent_selection(headless_like, session_);

    const auto credentials = ava::app::load_credentials_for_run();
    provider_ = ava::app::build_provider_for_run(selection_, credentials);

    ava::tools::register_default_tools(registry_, std::filesystem::current_path());
    std::shared_ptr<ava::tools::ApprovalBridge> approval_bridge;
    if(options_.auto_approve) {
      approval_bridge = std::make_shared<AllowAllApprovalBridge>();
    }
    registry_.add_middleware(std::make_shared<ava::tools::PermissionMiddleware>(
        std::make_shared<ava::tools::DefaultHeadlessPermissionInspector>(),
        std::move(approval_bridge)
    ));

    runtime_ = std::make_unique<ava::agent::AgentRuntime>(
        *provider_,
        registry_,
        ava::agent::AgentConfig{.max_turns = selection_.max_turns}
    );

    const auto intro = fmt::format(
        "session={} provider={} model={}{}",
        session_.id,
        selection_.provider,
        selection_.model,
        options_.auto_approve ? " auto_approve=on" : ""
    );
    state_.set_status_line(intro);
  }

  ~TuiApp() {
    if(run_thread_.joinable()) {
      run_thread_.detach();
    }
  }

  int run() {
    using namespace ftxui;

    ScreenInteractive screen = ScreenInteractive::Fullscreen();
    screen_.store(&screen, std::memory_order_release);

    auto renderer = Renderer([&] {
      std::lock_guard lock(state_mutex_);
      state_.set_viewport_rows(12);

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

      return vbox({
                 window(text("Messages"), vbox(std::move(message_rows)) | frame | vscroll_indicator) | flex,
                 separator(),
                 window(text(composer_label), text(input.empty() ? "" : input)),
                 separator(),
                 text(status),
                 text("Enter=submit  Up/Down/PgUp/PgDn=scroll  q=quit"),
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
            state_.set_status_line("Run in progress. Will quit when this run finishes.");
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
        state_.scroll_up(1);
        return true;
      }
      if(event == ftxui::Event::ArrowDown) {
        std::lock_guard lock(state_mutex_);
        state_.scroll_down(1);
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

      if(event == ftxui::Event::Return) {
        submit_from_ui();
        return true;
      }

      return false;
    });

    screen.Loop(component);
    screen_.store(nullptr, std::memory_order_release);

    if(run_thread_.joinable()) {
      run_thread_.join();
    }

    sessions_.save(session_);
    return 0;
  }

  private:
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

    if(run_thread_.joinable()) {
      run_thread_.join();
    }

    auto self = shared_from_this();
    run_thread_ = std::thread([self = std::move(self), prompt = *prompt] {
      const auto result = self->runtime_->run(
          self->session_,
          ava::agent::AgentRunInput{.goal = prompt, .queue = &self->queue_},
          [&](const ava::agent::AgentEvent& event) {
            {
              std::lock_guard lock(self->state_mutex_);
              self->state_.apply_agent_event(event);
            }
            self->post_custom_event();
          }
      );

      {
        std::lock_guard lock(self->state_mutex_);
        if(result.error.has_value()) {
          self->state_.set_running(false);
          self->state_.set_status_line("Run failed: " + *result.error);
        }
      }

      self->post_custom_event();

      if(self->quit_when_run_finishes_.load()) {
        {
          std::lock_guard lock(self->state_mutex_);
          self->state_.request_quit();
        }
        if(auto* screen = self->screen_.load(std::memory_order_acquire); screen != nullptr) {
          screen->PostEvent(ftxui::Event::Custom);
        }
      }
    });
  }

  TuiOptions options_;
  ava::session::SessionManager sessions_;
  ava::types::SessionRecord session_;
  ava::app::ResolvedAgentSelection selection_;
  ava::llm::ProviderPtr provider_;
  ava::tools::ToolRegistry registry_;
  ava::agent::MessageQueue queue_;
  std::unique_ptr<ava::agent::AgentRuntime> runtime_;

  ava::tui::AppState state_;
  std::mutex state_mutex_;
  std::thread run_thread_;
  std::atomic<bool> quit_when_run_finishes_{false};

  std::atomic<ftxui::ScreenInteractive*> screen_{nullptr};
};
#endif

}  // namespace

int main(int argc, char** argv) {
  TuiOptions options;
  try {
    options = parse_tui_options_or_throw(argc, argv);
  } catch(const CLI::CallForHelp& e) {
    fmt::print("{}\n", e.what());
    return 0;
  } catch(const std::exception& ex) {
    fmt::print(stderr, "error: {}\n", ex.what());
    return 2;
  }

#if AVA_WITH_FTXUI
  try {
    auto app = std::make_shared<TuiApp>(std::move(options));
    return app->run();
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
