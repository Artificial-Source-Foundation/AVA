#pragma once

#include <threadsafe/threadsafe.h>

namespace ava::app::runtime {

class Session;
using session_ts = threadsafe::Unlocked<Session, threadsafe::policy::Primitive<std::mutex>>;

} // namespace ava::app::runtime
