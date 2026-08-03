#pragma once

#include <threadsafe/threadsafe.h>

namespace ava::app::runtime {

class Session;
using session_ts = threadsafe::Unlocked<Session, threadsafe::policy::Primitive<std::mutex>>;

} // namespace ava::app::runtime

// Locks `unlocked_session` for reading; use `session_r` to access the Session.
// This can only be used once per scope. See CRITICAL_AREA_CONTINUE_R.
#define CRITICAL_AREA_BEGIN_R(session) \
  ava::app::runtime::session_ts::rat session##_r(unlocked_##session)

// Unlock `session_r`. Using it after this will lead to a SEGFAULT.
#define CRITICAL_AREA_END_R(session) \
  do { session##_r.unlock(); } while(0)

// Locks `unlocked_session` again for reading after the use of CRITICAL_AREA_END_R; use `session_r` to access the Session.
#define CRITICAL_AREA_CONTINUE_R(session) \
  do { session##_r.relock(unlocked_##session); } while(0)

// Locks `unlocked_session` for reading and writing; use `session_w` to access the Session.
// This can only be used once per scope. See CRITICAL_AREA_CONTINUE_W.
#define CRITICAL_AREA_BEGIN_W(session) \
  ava::app::runtime::session_ts::wat session##_w(unlocked_##session)

// Unlock `session_w`. Using it after this will lead to a SEGFAULT.
#define CRITICAL_AREA_END_W(session) \
  do { session##_w.unlock(); } while(0)

// Locks `unlocked_session` again for reading and writing after the use of CRITICAL_AREA_END_W; use `session_w` to access the Session.
#define CRITICAL_AREA_CONTINUE_W(session) \
  do { session##_w.relock(unlocked_##session); } while(0)

// Use this instead of CRITICAL_AREA_BEGIN_R if `unlocked_session` is a const&.
#define CRITICAL_AREA_BEGIN_CR(session) \
  ava::app::runtime::session_ts::crat session##_r(unlocked_##session)

