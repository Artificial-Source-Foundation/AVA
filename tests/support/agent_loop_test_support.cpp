#include "sys.h"
#include "tests/support/agent_loop_test_support.h"
#include "ava/http/transport.h"
#include "ava/core/json.h"

#include <expected>
#include <utility>

namespace agent_loop_test {

void TraceCollector::on_event(ava::observability::TraceEvent const& event)
{
  std::lock_guard lock(mutex);
  events.push_back(event);
}

ava::http::HttpResponse sse_response(std::string const& body)
{
  return ava::http::HttpResponse{.status_code = 200, .headers = {}, .body = body};
}

std::string tool_call_sse(std::string_view id, std::string_view name, std::string_view arguments_json)
{
  return "data: {\"type\":\"response.function_call.added\",\"call_id\":\"" + ava::core::json::escape(id) + "\",\"name\":\"" + ava::core::json::escape(name) +
         "\"}\n\n" + "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"" + ava::core::json::escape(id) + "\",\"delta\":\"" +
         ava::core::json::escape(arguments_json) + "\"}\n\n" + "data: {\"type\":\"response.function_call.done\",\"call_id\":\"" + ava::core::json::escape(id) +
         "\"}\n\n";
}

ava::agent::ModelInvocationOptions model_invocation_options(std::string system_prompt, std::string provider_id, std::string model_id)
{
  return ava::agent::ModelInvocationOptions{.provider_id = std::move(provider_id), .model_id = std::move(model_id), .system_prompt = std::move(system_prompt)};
}

SharedFakeTransport::SharedFakeTransport(std::shared_ptr<std::vector<ava::http::HttpResponse>> responses,
                                         std::shared_ptr<std::vector<ava::http::HttpRequest>> requests, std::shared_ptr<std::mutex> mutex)
    : responses_(std::move(responses)), requests_(std::move(requests)), mutex_(std::move(mutex))
{
}

ava::core::Result<ava::http::HttpResponse> SharedFakeTransport::send(ava::http::HttpRequest const& request)
{
  std::lock_guard lock(*mutex_);
  requests_->push_back(request);
  if (responses_->empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "fake transport has no response"));
  }
  auto response = responses_->front();
  responses_->erase(responses_->begin());
  return response;
}

void BlockingBackgroundTransport::State::release_success()
{
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  changed.notify_all();
}

void BlockingBackgroundTransport::State::notify()
{
  changed.notify_all();
}

bool BlockingBackgroundTransport::State::wait_for_request(std::chrono::milliseconds timeout)
{
  std::unique_lock lock(mutex);
  return changed.wait_for(lock, timeout, [&] { return request_seen; });
}

bool BlockingBackgroundTransport::State::wait_for_cancel(std::chrono::milliseconds timeout)
{
  std::unique_lock lock(mutex);
  return changed.wait_for(lock, timeout, [&] { return cancel_observed; });
}

std::vector<ava::http::HttpRequest> BlockingBackgroundTransport::State::requests_snapshot()
{
  std::lock_guard lock(mutex);
  return requests;
}

BlockingBackgroundTransport::BlockingBackgroundTransport(std::shared_ptr<State> state, ava::http::HttpResponse response)
    : state_(std::move(state)), response_(std::move(response))
{
}

ava::core::Result<ava::http::HttpResponse> BlockingBackgroundTransport::send(ava::http::HttpRequest const& request)
{
  return send(request, nullptr);
}

ava::core::Result<ava::http::HttpResponse> BlockingBackgroundTransport::send(ava::http::HttpRequest const& request, CancelCallback cancel_requested)
{
  std::unique_lock lock(state_->mutex);
  state_->requests.push_back(request);
  state_->request_seen = true;
  state_->changed.notify_all();
  state_->changed.wait(lock, [&] { return state_->release || (cancel_requested && cancel_requested()); });
  if (cancel_requested && cancel_requested())
  {
    state_->cancel_observed = true;
    state_->changed.notify_all();
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  return response_;
}

}  // namespace agent_loop_test
