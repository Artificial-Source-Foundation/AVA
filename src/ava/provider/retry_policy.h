#pragma once

#include <cstddef>
#include <string_view>

#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] bool is_retryable_kind(ProviderErrorKind kind) noexcept;
[[nodiscard]] bool is_retryable_transport_error(ava::core::Error const& error) noexcept;
[[nodiscard]] int exponential_delay_ms(RetryOptions const& options, int attempt) noexcept;
[[nodiscard]] bool retry_cancel_requested(RetryOptions const& options,
                                          Transport::CancelCallback const& cancel_requested);
[[nodiscard]] ava::core::Error retry_canceled_error();
[[nodiscard]] std::optional<int> retry_after_ms(HttpResponse const& response, int max_retry_after_ms);
[[nodiscard]] ava::core::VoidResult publish_retry_event(RetryOptions const& options, std::size_t attempt,
                                                        std::size_t max_attempts, int delay_ms,
                                                        std::size_t remaining_ms, std::string_view reason,
                                                        int status_code, bool streaming, bool countdown_tick = false);
[[nodiscard]] ava::core::VoidResult sleep_before_retry(RetryOptions const& options, std::size_t attempt,
                                                       std::size_t max_attempts, int delay_ms, std::string_view reason,
                                                       int status_code, bool streaming,
                                                       Transport::CancelCallback const& cancel_requested = nullptr);

}  // namespace ava::provider::detail
