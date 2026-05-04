#include "ava/app/runtime_retry.h"

#include <mutex>

#include "ava/session/session_store.h"

namespace ava::app::runtime {
namespace {

RuntimeEvent base_retry_event(RuntimeSession const& session, RuntimeRunOptions const& options) {
  auto build = [&] {
    RuntimeEvent event;
    event.type = RuntimeEventType::Retry;
    event.timestamp = ava::session::now_timestamp();
    event.session_id = session.store.session_id();
    event.mode = session.mode;
    event.provider_id = session.model.provider_id;
    event.model_id = session.model.model_id;
    event.trigger = "provider_transport";
    return event;
  };
  if (!options.session_mutex) return build();
  std::lock_guard lock(*options.session_mutex);
  return build();
}

}  // namespace

ava::provider::RetryOptions runtime_retry_options(RuntimeSession const& session, RuntimeRunOptions const& options) {
  ava::provider::RetryOptions retry_options;
  retry_options.cancel_requested = options.cancel_requested;
  retry_options.on_retry = [&session, &options](ava::provider::RetryOptions::Event const& retry) {
    auto event = base_retry_event(session, options);
    event.type = retry.countdown_tick ? RuntimeEventType::RetryTick : RuntimeEventType::Retry;
    event.reason = retry.reason;
    event.status = retry.streaming ? "streaming" : "request";
    event.attempt = retry.attempt;
    event.max_attempts = retry.max_attempts;
    event.delay_ms = retry.delay_ms;
    event.remaining_ms = retry.remaining_ms;
    if (retry.status_code > 0) {
      event.text = "HTTP status " + std::to_string(retry.status_code);
    }
    return emit_event(options.event_sink, event);
  };
  return retry_options;
}

}  // namespace ava::app::runtime
