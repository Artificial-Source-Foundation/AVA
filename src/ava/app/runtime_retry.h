#pragma once

#include "ava/app/runtime.h"
#include "ava/provider/provider.h"

namespace ava::app::runtime {

[[nodiscard]] ava::provider::RetryOptions runtime_retry_options(runtime::Session const& session, runtime::RunOptions const& options);

}  // namespace ava::app::runtime
