#pragma once

#include "ava/debug/print_members_on.h"

#include <threadsafe/threadsafe.h>
#ifdef CWDEBUG
#include <array>
#include <source_location>
#include <string_view>
#include <threadsafe/AIMutex.h>
#endif

namespace ava::app::runtime {

class Session;

#ifdef CWDEBUG
// Track session mutex ownership by the current thread while retaining AIMutex's self-deadlock detection.
//
// Every successful lock acquisition registers this mutex in thread-local state. Potentially blocking code can therefore reject execution while the
// current thread owns any session mutex, even when that code has no reference to the corresponding session_ts object.
class SessionDebugMutex final : public AIMutex
{
 private:
  static constexpr std::size_t max_nested_session_locks = 32;
  inline static thread_local std::array<SessionDebugMutex const*, max_nested_session_locks> held_mutexes_{};
  inline static thread_local std::size_t held_mutex_count_ = 0;

  // Register mutex after its underlying AIMutex has been acquired successfully.
  //
  // The fixed-capacity thread-local registry cannot allocate or throw while the mutex is held. Different session mutexes may be nested, but recursive
  // acquisition of one mutex remains rejected by AIMutex.
  static void register_lock(SessionDebugMutex const* mutex)
  {
    ASSERT(held_mutex_count_ < held_mutexes_.size());
    held_mutexes_[held_mutex_count_++] = mutex;
  }

  // Remove mutex from the current thread's registry before releasing its underlying AIMutex.
  //
  // Removal does not require LIFO unlock order because separately owned session guards can have independent scopes.
  static void unregister_lock(SessionDebugMutex const* mutex)
  {
    std::size_t index = 0;
    while (index < held_mutex_count_ && held_mutexes_[index] != mutex) ++index;
    ASSERT(index < held_mutex_count_);
    held_mutexes_[index] = held_mutexes_[--held_mutex_count_];
    held_mutexes_[held_mutex_count_] = nullptr;
  }

 public:
  // Acquire this session mutex and register it as owned by the current thread.
  void lock()
  {
    PRAGMA_DIAGNOSTIC_PUSH_IGNORE_frame_address
    DoutEntering(dc::notice, "SessionDebugMutex::lock() [" << this << "] called from " << Location((char*)__builtin_return_address(3) + builtin_return_address_offset));
    PRAGMA_DIAGNOSTIC_POP

    AIMutex::lock();
    register_lock(this);

    Dout(dc::notice, "Locked SessionDebugMutex [" << this << "]");
  }

  // Try to acquire this session mutex, registering only a successful acquisition.
  //
  // Returns true only when the underlying mutex was acquired.
  [[nodiscard]] bool try_lock()
  {
    DoutEntering(dc::notice, "SessionDebugMutex::try_lock() [" << this << "]");

    if (!AIMutex::try_lock())
      return false;
    register_lock(this);
    Dout(dc::notice, "Locked SessionDebugMutex (try_lock) [" << this << "]");
    return true;
  }

  // Release this session mutex and remove it from the current thread's registry.
  void unlock()
  {
    DoutEntering(dc::notice, "SessionDebugMutex::unlock() [" << this << "]");

    ASSERT(is_self_locked());
    unregister_lock(this);
    AIMutex::unlock();
  }

  // Return whether the current thread owns at least one debug session mutex.
  [[nodiscard]] static bool current_thread_holds_session_lock() noexcept { return held_mutex_count_ != 0; }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

template<typename SESSION>
class UnlockedSession : public threadsafe::Unlocked<SESSION, threadsafe::policy::Primitive<SessionDebugMutex>>
{
 public:
  using threadsafe::Unlocked<SESSION, threadsafe::policy::Primitive<SessionDebugMutex>>::Unlocked;
  using threadsafe::Unlocked<SESSION, threadsafe::policy::Primitive<SessionDebugMutex>>::mutex;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

using session_ts = UnlockedSession<Session>;
#else
using session_ts = threadsafe::Unlocked<Session, threadsafe::policy::Primitive<std::mutex>>;
#endif

#ifdef CWDEBUG
// Assert that the current thread owns no session mutex before entering a potentially blocking operation.
//
// Operation describes the wait or other slow boundary. Location identifies the assertion call site; this function does not require access to a
// particular session_ts because SessionDebugMutex maintains thread-local ownership state for every session.
inline void assert_no_session_lock_held(std::string_view operation, std::source_location location = std::source_location::current())
{
  bool const held = SessionDebugMutex::current_thread_holds_session_lock();
  if (held)
    DoutFatal(dc::coredump,
        "Potentially blocking operation while holding a session mutex: " << operation << " at " << location.file_name() << ':' << location.line());
}

// Assert that the current thread does not own the mutex wrapped by unlocked_session before entering a potentially blocking operation.
//
// Operation describes the wait or other slow boundary. This narrower check complements assert_no_session_lock_held when the relevant session is
// already part of the API contract.
template <typename UnlockedSession>
inline void assert_session_unlocked(UnlockedSession const& unlocked_session, std::string_view operation,
                                    std::source_location location = std::source_location::current())
{
  bool const held = unlocked_session.mutex().is_self_locked();
  if (held)
    DoutFatal(dc::coredump,
        "Session mutex still locked while: " << operation << " at " << location.file_name() << ':' << location.line());
}
#endif

}  // namespace ava::app::runtime

#ifdef CWDEBUG
#define AVA_ASSERT_NO_SESSION_LOCK_HELD(operation) ::ava::app::runtime::assert_no_session_lock_held(operation)
#define AVA_ASSERT_SESSION_UNLOCKED(unlocked_session, operation) ::ava::app::runtime::assert_session_unlocked(unlocked_session, operation)
#else
#define AVA_ASSERT_NO_SESSION_LOCK_HELD(operation) ((void)0)
#define AVA_ASSERT_SESSION_UNLOCKED(unlocked_session, operation) ((void)0)
#endif

#ifndef CWDEBUG
#define AVA_DEBUG_PRINT_MESSAGE(msg)
#else
#define AVA_DEBUG_PRINT_MESSAGE(msg) \
  ; Dout(dc::notice, msg << " from " __FILE__ << ":" << __LINE__)
#endif

#define AVA_DECLARE_ACCESS_TYPE_CR(session_r, unlocked_session) \
  ava::app::runtime::session_ts::crat session_r(unlocked_session)

#define AVA_DECLARE_ACCESS_TYPE_R(session_r, unlocked_session) \
  ava::app::runtime::session_ts::rat session_r(unlocked_session)

#define AVA_DECLARE_ACCESS_TYPE_W(session_w, unlocked_session) \
  ava::app::runtime::session_ts::wat session_w(unlocked_session)

// Use this instead of CRITICAL_AREA_BEGIN_R if `unlocked_session` is a const&.
#define CRITICAL_AREA_BEGIN_CR(session) \
  AVA_DECLARE_ACCESS_TYPE_CR(session##_r, unlocked_##session) \
  AVA_DEBUG_PRINT_MESSAGE("Created session##_r from unlocked_##session")

// Locks `unlocked_session` for reading; use `session_r` to access the Session.
// This can only be used once per scope. See CRITICAL_AREA_CONTINUE_R.
#define CRITICAL_AREA_BEGIN_R(session) \
  AVA_DECLARE_ACCESS_TYPE_R(session##_r, unlocked_##session) \
  AVA_DEBUG_PRINT_MESSAGE("Created session##_r from unlocked_##session")

// Unlock `session_r`. Using it after this will lead to a SEGFAULT.
#define CRITICAL_AREA_END_R(session) \
  do { session##_r.unlock() AVA_DEBUG_PRINT_MESSAGE("Unlocked session##_r"); } while(0)

// Locks `unlocked_session` again for reading after the use of CRITICAL_AREA_END_R; use `session_r` to access the Session.
#define CRITICAL_AREA_CONTINUE_R(session) \
  do { session##_r.relock(unlocked_##session) AVA_DEBUG_PRINT_MESSAGE("Relocked session##_r"); } while(0)

// Locks `unlocked_session` for reading and writing; use `session_w` to access the Session.
// This can only be used once per scope. See CRITICAL_AREA_CONTINUE_W.
#define CRITICAL_AREA_BEGIN_W(session) \
  AVA_DECLARE_ACCESS_TYPE_W(session##_w, unlocked_##session) \
  AVA_DEBUG_PRINT_MESSAGE("Created session##_r from unlocked_##session")

// Unlock `session_w`. Using it after this will lead to a SEGFAULT.
#define CRITICAL_AREA_END_W(session) \
  do { session##_w.unlock() AVA_DEBUG_PRINT_MESSAGE("Unlocked session##_r"); } while(0)

// Locks `unlocked_session` again for reading and writing after the use of CRITICAL_AREA_END_W; use `session_w` to access the Session.
#define CRITICAL_AREA_CONTINUE_W(session) \
  do { session##_w.relock(unlocked_##session) AVA_DEBUG_PRINT_MESSAGE("Relocked session##_w"); } while(0)

// Aliases used when the lock just runs till the end of the scope (no CRITICAL_AREA_END_*).
#define SCOPED_CRITICAL_AREA_CR(session_r, unlocked_session) \
  AVA_DECLARE_ACCESS_TYPE_CR(session_r, unlocked_session) \
  AVA_DEBUG_PRINT_MESSAGE("Created " #session_r " from " #unlocked_session " (scoped)")

#define SCOPED_CRITICAL_AREA_R(session_r, unlocked_session) \
  AVA_DECLARE_ACCESS_TYPE_R(session_r, unlocked_session) \
  AVA_DEBUG_PRINT_MESSAGE("Created " #session_r " from " #unlocked_session " (scoped)")

#define SCOPED_CRITICAL_AREA_W(session_w, unlocked_session) \
  AVA_DECLARE_ACCESS_TYPE_W(session_w, unlocked_session) \
  AVA_DEBUG_PRINT_MESSAGE("Created " #session_w " from " #unlocked_session " (scoped)")
