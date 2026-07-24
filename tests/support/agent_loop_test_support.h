#pragma once

#include "ava/observability/run_observer.h"
#include "ava/provider/provider.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace agent_loop_test {

class TraceCollector final : public ava::observability::RunObserver
{
 public:
  void on_event(ava::observability::TraceEvent const& event) override;

  std::mutex mutex;
  std::vector<ava::observability::TraceEvent> events;
};

ava::provider::HttpResponse sse_response(std::string const& body);
std::string tool_call_sse(std::string_view id, std::string_view name, std::string_view arguments_json);

class SharedFakeTransport final : public ava::provider::Transport
{
 public:
  SharedFakeTransport(std::shared_ptr<std::vector<ava::provider::HttpResponse>> responses, std::shared_ptr<std::vector<ava::provider::HttpRequest>> requests,
                      std::shared_ptr<std::mutex> mutex);

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override;

 private:
  std::shared_ptr<std::vector<ava::provider::HttpResponse>> responses_;
  std::shared_ptr<std::vector<ava::provider::HttpRequest>> requests_;
  std::shared_ptr<std::mutex> mutex_;
};

class BlockingBackgroundTransport final : public ava::provider::Transport
{
 public:
  struct State
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool request_seen = false;
    bool release = false;
    bool cancel_observed = false;
    std::vector<ava::provider::HttpRequest> requests;

    void release_success();
    void notify();
    [[nodiscard]] bool wait_for_request(std::chrono::milliseconds timeout);
    [[nodiscard]] bool wait_for_cancel(std::chrono::milliseconds timeout);
    [[nodiscard]] std::vector<ava::provider::HttpRequest> requests_snapshot();
  };

  BlockingBackgroundTransport(std::shared_ptr<State> state, ava::provider::HttpResponse response);

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override;
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request, CancelCallback cancel_requested) override;

 private:
  std::shared_ptr<State> state_;
  ava::provider::HttpResponse response_;
};

}  // namespace agent_loop_test
