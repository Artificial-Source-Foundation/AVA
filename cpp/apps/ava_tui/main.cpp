#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <chrono>
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
#include "interactive_action_adapter.hpp"
#include "interactive_detail_projection.hpp"
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

[[nodiscard]] std::string interactive_dock_kind_label(ava::tui::InteractiveDockKind kind) {
  switch(kind) {
    case ava::tui::InteractiveDockKind::Approval:
      return "Tool Approval";
    case ava::tui::InteractiveDockKind::Question:
      return "Question";
    case ava::tui::InteractiveDockKind::Plan:
      return "Plan Approval";
  }
  return "Interactive";
}

[[nodiscard]] ftxui::Element render_interactive_dock(const ava::tui::InteractiveDockState& dock) {
  using namespace ftxui;
  std::vector<Element> rows;
  rows.push_back(text(interactive_dock_kind_label(dock.kind) + " pending: " + dock.request.request_id));
  if(dock.request.run_id.has_value()) {
    rows.push_back(text("run_id: " + *dock.request.run_id));
  }
  for(const auto& line : dock.detail_lines) {
    rows.push_back(text(line));
  }

  switch(dock.kind) {
    case ava::tui::InteractiveDockKind::Approval:
      rows.push_back(text(dock.approval_can_approve ? "y/Enter=approve  n/r/Esc=reject  q=cancel run"
                                                 : "n/r/Esc=reject  q=cancel run  approval disabled"));
      break;
    case ava::tui::InteractiveDockKind::Question:
      rows.push_back(text("Answer: " + dock.answer_draft));
      rows.push_back(text("Type answer  Enter=submit  Esc=cancel question"));
      break;
    case ava::tui::InteractiveDockKind::Plan:
      rows.push_back(text("y/Enter=accept  n/r/Esc=reject  q=cancel run"));
      break;
  }

  return window(text("Interactive Request"), vbox(std::move(rows))) | border;
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
            .approval_resolver = [this](
                                     const ava::control_plane::InteractiveRequestHandle& handle,
                                     const ava::orchestration::ApprovalRequestPayload& payload
                                 ) {
              return wait_for_approval_resolution(handle, payload);
            },
            .question_resolver = [this](
                                     const ava::control_plane::InteractiveRequestHandle& handle,
                                     const ava::orchestration::QuestionRequestPayload& payload
                                 ) {
              return wait_for_question_resolution(handle, payload);
            },
            .plan_resolver = [this](
                                 const ava::control_plane::InteractiveRequestHandle& handle,
                                 const ava::orchestration::PlanRequestPayload& payload
                             ) {
              return wait_for_plan_resolution(handle, payload);
            },
            .provider_override = nullptr,
            .provider_factory = nullptr,
            .credentials_override = std::nullopt,
            .load_global_mcp_config = true,
        })),
        adapter_(composition_.interactive_bridge) {

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
    request_interactive_cancel_from_ui("TUI shutting down; cancelling pending interactive request.", false);
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
      const auto dock = state_.active_interactive_dock();

      std::vector<Element> root_rows;
      root_rows.push_back(window(text("Messages"), vbox(std::move(message_rows)) | frame | vscroll_indicator) | flex);
      root_rows.push_back(separator());
      if(dock.has_value()) {
        root_rows.push_back(render_interactive_dock(*dock));
      } else {
        root_rows.push_back(window(text(composer_label), text(input.empty() ? "" : input)));
      }
      root_rows.push_back(separator());
      root_rows.push_back(text(navigation_line));
      root_rows.push_back(text(interactive_line));
      root_rows.push_back(text(status));
      root_rows.push_back(text("Enter=submit  Up/Down=history  PgUp/PgDn/Home/End=messages  q=quit"));

      return vbox(std::move(root_rows)) | border;
    });

    auto component = CatchEvent(renderer, [&](const ftxui::Event& event) {
      if(event == ftxui::Event::Custom) {
        std::lock_guard lock(state_mutex_);
        if(state_.quit_requested() && !state_.running()) {
          screen.ExitLoopClosure()();
          return true;
        }
      }

      if(handle_interactive_dock_event(event)) {
        return true;
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
            interactive_cancel_requested_.store(true);
            interactive_resolution_cv_.notify_all();
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
      request_interactive_cancel_from_ui("TUI loop failed; cancelling pending interactive request.", false);
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

  void request_interactive_cancel_from_ui(const std::string& status, bool request_quit) {
    if(request_quit) {
      quit_when_run_finishes_.store(true);
    }
    interactive_cancel_requested_.store(true);
    interactive_resolution_cv_.notify_all();
    if(const auto cancel_handle = current_cancel_handle(); cancel_handle.has_value()) {
      cancel_handle->cancel();
    }
    {
      std::lock_guard lock(state_mutex_);
      state_.set_status_line(status);
    }
    post_custom_event();
  }

  [[nodiscard]] bool handle_interactive_dock_event(const ftxui::Event& event) {
    std::optional<ava::tui::InteractiveAdapterAction> action;
    bool handled = false;

    {
      std::lock_guard lock(state_mutex_);
      const auto& dock = state_.active_interactive_dock();
      if(!dock.has_value()) {
        return false;
      }

      if(event.is_character() && event.character() == "q" && dock->kind != ava::tui::InteractiveDockKind::Question) {
        handled = true;
      }

      if(!handled && event == ftxui::Event::Escape) {
        action = state_.reject_interactive_dock_action("cancelled from TUI dock");
        handled = true;
      }

      if(!handled && event == ftxui::Event::Backspace) {
        if(dock->kind == ava::tui::InteractiveDockKind::Question) {
          state_.backspace_interactive_answer();
        }
        return true;
      }

      if(!handled && event == ftxui::Event::Return) {
        switch(dock->kind) {
          case ava::tui::InteractiveDockKind::Approval:
            action = state_.approve_interactive_dock_action();
            handled = true;
            break;
          case ava::tui::InteractiveDockKind::Question:
            action = state_.answer_interactive_dock_action();
            handled = true;
            break;
          case ava::tui::InteractiveDockKind::Plan:
            action = state_.accept_plan_interactive_dock_action();
            handled = true;
            break;
        }
      } else if(!handled && event.is_character()) {
        const auto character = event.character();
        handled = true;
        switch(dock->kind) {
          case ava::tui::InteractiveDockKind::Approval:
            if(character == "y" || character == "a") {
              action = state_.approve_interactive_dock_action();
              handled = true;
            } else if(character == "n" || character == "r") {
              action = state_.reject_interactive_dock_action("rejected from TUI dock");
              handled = true;
            }
            break;
          case ava::tui::InteractiveDockKind::Question:
            state_.insert_interactive_answer_text(character);
            return true;
          case ava::tui::InteractiveDockKind::Plan:
            if(character == "y" || character == "a") {
              action = state_.accept_plan_interactive_dock_action();
              handled = true;
            } else if(character == "n" || character == "r") {
              action = state_.reject_interactive_dock_action("plan rejected from TUI dock");
              handled = true;
            }
            break;
        }
      }
    }

    if(!handled) {
      return false;
    }

    if(!action.has_value() && event.is_character() && event.character() == "q") {
      request_interactive_cancel_from_ui("Run cancellation requested. Waiting for cooperative stop...", true);
      return true;
    }

    if(!action.has_value()) {
      std::lock_guard lock(state_mutex_);
      state_.set_status_line("Interactive action unavailable for the current dock.");
      return true;
    }

    const auto result = adapter_.apply(*action);
    std::lock_guard lock(state_mutex_);
    state_.apply_interactive_action_result(result);
    sync_interactive_state_locked();
    interactive_resolution_cv_.notify_all();
    return true;
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
    interactive_cancel_requested_.store(false);
    {
      std::lock_guard lock(state_mutex_);
      state_.clear_quit_request();
    }

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
        state_.set_running(false);
        if(result.error.has_value()) {
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

  [[nodiscard]] std::optional<ava::orchestration::AdapterResolutionRecord> wait_for_adapter_resolution(
      const ava::control_plane::InteractiveRequestHandle& handle
  ) {
    std::unique_lock lock(interactive_resolution_mutex_);
    while(!interactive_cancel_requested_.load()) {
      if(auto record = composition_.interactive_bridge->adapter_resolution_for(handle.request_id); record.has_value()) {
        return record;
      }
      interactive_resolution_cv_.wait_for(lock, std::chrono::milliseconds(100));
    }
    if(auto record = composition_.interactive_bridge->adapter_resolution_for(handle.request_id); record.has_value()) {
      return record;
    }
    return std::nullopt;
  }

  [[nodiscard]] ava::orchestration::ApprovalResolution wait_for_approval_resolution(
      const ava::control_plane::InteractiveRequestHandle& handle,
      const ava::orchestration::ApprovalRequestPayload& payload
  ) {
    {
      std::lock_guard lock(state_mutex_);
      const auto projection = approval_detail_projection(payload);
      state_.set_interactive_request_details(handle.request_id, projection.lines);
      state_.set_interactive_approval_can_approve(handle.request_id, projection.complete);
    }
    post_custom_event();
    const auto record = wait_for_adapter_resolution(handle);
    if(!record.has_value()) {
      return ava::orchestration::ApprovalResolution{
          .approval = ava::tools::ToolApproval::rejected("run cancelled while waiting for TUI approval"),
          .state = ava::control_plane::InteractiveRequestState::Cancelled,
      };
    }
    if(record->approval.has_value()) {
      return ava::orchestration::ApprovalResolution{.approval = *record->approval, .state = record->state};
    }
    return ava::orchestration::ApprovalResolution{
        .approval = ava::tools::ToolApproval::rejected("approval request resolved without approval payload"),
        .state = record->state,
    };
  }

  [[nodiscard]] ava::orchestration::QuestionResolution wait_for_question_resolution(
      const ava::control_plane::InteractiveRequestHandle& handle,
      const ava::orchestration::QuestionRequestPayload& payload
  ) {
    {
      std::lock_guard lock(state_mutex_);
      state_.set_interactive_request_details(handle.request_id, question_detail_lines(payload));
    }
    post_custom_event();
    const auto record = wait_for_adapter_resolution(handle);
    if(!record.has_value()) {
      return ava::orchestration::QuestionResolution{.answer = std::nullopt, .state = ava::control_plane::InteractiveRequestState::Cancelled};
    }
    return ava::orchestration::QuestionResolution{.answer = record->answer, .state = record->state};
  }

  [[nodiscard]] ava::orchestration::PlanResolution wait_for_plan_resolution(
      const ava::control_plane::InteractiveRequestHandle& handle,
      const ava::orchestration::PlanRequestPayload& payload
  ) {
    {
      std::lock_guard lock(state_mutex_);
      state_.set_interactive_request_details(handle.request_id, plan_detail_lines(payload));
    }
    post_custom_event();
    const auto record = wait_for_adapter_resolution(handle);
    if(!record.has_value()) {
      return ava::orchestration::PlanResolution{.accepted = false, .state = ava::control_plane::InteractiveRequestState::Cancelled};
    }
    return ava::orchestration::PlanResolution{.accepted = record->plan_accepted.value_or(false), .state = record->state};
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
  ava::tui::InteractiveActionAdapter adapter_;

  ava::tui::AppState state_;
  std::mutex state_mutex_;
  mutable std::mutex run_control_mutex_;
  mutable std::mutex interactive_resolution_mutex_;
  mutable std::condition_variable interactive_resolution_cv_;
  std::thread run_thread_;
  std::optional<ava::orchestration::RunCancellationHandle> current_cancel_handle_;
  std::atomic<bool> quit_when_run_finishes_{false};
  std::atomic<bool> interactive_cancel_requested_{false};

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
