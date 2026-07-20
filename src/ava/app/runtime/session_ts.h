#pragma once

#include <threadsafe/threadsafe.h>

namespace ava::app::runtime {

struct Session;
using session_ts = threadsafe::Unlocked<Session, threadsafe::policy::Primitive<std::mutex>>;

} // namespace ava::app::runtime
