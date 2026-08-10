#pragma once
#include "ava/http/transport.h"
#include "ava/app/runtime.h"
#include "ava/provider/provider.h"

namespace ava::app::runtime {

[[nodiscard]] ava::http::RetryOptions runtime_retry_options(session_ts const& unlocked_session, RunOptions const& options);

}  // namespace ava::app::runtime
