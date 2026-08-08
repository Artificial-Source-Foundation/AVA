#pragma once

#include "ava/debug/print_members_on.h"

// ava/core/thread.h
//
// Centralized thread creation that always registers the new thread with libcwd
// before any user code runs on it.
//
// libcwd requires that, as the very first thing a new thread does, it calls
//   Debug(NAMESPACE_DEBUG::init_thread("<label>"));
// so that debug output can be attributed to a named thread. Spelling that line
// out at the top of every worker lambda is error-prone (easy to forget on a new
// thread) and was previously only done at the entry of run_rpc_loop, which is no
// longer a reliable thread entry point since it is also called on the main
// thread.
//
// Instead, all AVA thread creation goes through the helpers in this header:
//
//   ava::core::JoinThread::create("run_rpc_loop", [](std::stop_token stop) { ... });
//   ava::core::make_thread("acp_writer",    [this] { writer_loop(); });
//   ava::core::make_async("openai_browser", [&] { return do_oauth(...); });
//
// They inject init_thread() as the literal first statement of the new thread,
// and the label is mandatory by construction: there is no overload that lets a
// thread be started through this API without being named.
//
// init_thread() itself is defined out-of-line in thread.cpp (the only place that
// includes libcwd's debug.h and expands the Debug(...) macro), so this header
// stays free of any libcwd dependency. The helpers therefore work unchanged
// whether or not libcwd (CWDEBUG) is enabled: with libcwd disabled the macro
// expands to nothing and init_thread() is a no-op.

#include <array>
#include <cstddef>
#include <future>
#include <source_location>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include "debug.h"      // DEBUG_ONLY

namespace ava::core {

#ifdef CWDEBUG
namespace detail {
inline constexpr std::size_t max_nested_long_wait_incompatible_locks = 32;
using utils::has_print_on::operator<<;
struct LongWaitIncompatibleLockRegistry
{
  std::array<void const*, max_nested_long_wait_incompatible_locks> locks{};
  std::size_t count = 0;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

  void print_on(std::ostream& os) const
  {
    char const* sep = "";
    for (size_t i = 0; i < count; ++i)
    {
      os << sep << locks[i];
      sep = ", ";
    }
  }
};
inline thread_local LongWaitIncompatibleLockRegistry long_wait_incompatible_locks;
}  // namespace detail

// Register a debug-only lock whose ownership forbids potentially unbounded waits on the current thread.
//
// The lock identity must remain alive until unregister_long_wait_incompatible_lock is called. Registration is allocation-free and supports nesting
// different locks; registering more than 32 locks or unregistering an unowned lock is an invariant failure.
inline void register_long_wait_incompatible_lock(void const* lock)
{
  auto& registry = detail::long_wait_incompatible_locks;
  ASSERT(registry.count < registry.locks.size());
  registry.locks[registry.count++] = lock;
}

// Unregister a debug-only lock previously registered by the current thread.
//
// Locks may be released in a different order from acquisition. The lock identity must currently be registered exactly once.
inline void unregister_long_wait_incompatible_lock(void const* lock)
{
  auto& registry = detail::long_wait_incompatible_locks;
  std::size_t index = 0;
  while (index < registry.count && registry.locks[index] != lock) ++index;
  ASSERT(index < registry.count);
  registry.locks[index] = registry.locks[--registry.count];
  registry.locks[registry.count] = nullptr;
}

// Return whether the current thread owns a lock that forbids potentially unbounded waits.
[[nodiscard]] inline bool current_thread_holds_long_wait_incompatible_lock() noexcept
{
  return detail::long_wait_incompatible_locks.count != 0;
}

// Reject a potentially unbounded wait while the current thread owns an incompatible debug lock.
//
// Operation describes the wait. Location defaults to the caller so failures identify the boundary that attempted to block.
inline void assert_no_long_wait_incompatible_lock(std::string_view operation, std::source_location location = std::source_location::current())
{
  if (current_thread_holds_long_wait_incompatible_lock())
    DoutFatal(dc::coredump,
              "Potentially blocking operation while holding (an) incompatible lock(s) [" <<
              detail::long_wait_incompatible_locks << "]: " << operation << " at " << location.file_name() << ':' << location.line());
}
#endif

#ifdef CWDEBUG
// Register the calling thread with libcwd under `label`, so that debug output
// can be attributed to it and the thread is named.
//
// Returns void. No-op when libcwd (CWDEBUG) is not enabled.
void init_thread(std::string const& label);
#endif

// Own a cooperative thread and reject implicit or explicit joins while the current thread holds a debug lock that forbids long waits.
//
// create starts the thread after associating the mandatory human-readable label with its debug output. The callable receives a stop token when it
// accepts one and is otherwise called without arguments. Destruction and move assignment preserve std::jthread's request-stop-and-join behavior,
// but first assert that joining is allowed. Explicit join performs the same assertion; detach and stop requests do not wait.
//
// JoinThread is move-only. It intentionally has no conversion to std::jthread because conversion would discard join-boundary checking.
class JoinThread
{
 public:
  JoinThread() noexcept = default;
  ~JoinThread()
  {
    if (thread_.joinable())
      assert_join_allowed("implicitly joining JoinThread during destruction of thread");
  }

  JoinThread(JoinThread const&) = delete;
  JoinThread& operator=(JoinThread const&) = delete;
  JoinThread(JoinThread&&) noexcept = default;
  JoinThread& operator=(JoinThread&& other) noexcept
  {
    if (this != &other)
    {
      if (thread_.joinable())
        assert_join_allowed("implicitly joining JoinThread during move assignment into thread");
      thread_ = std::move(other.thread_);
#ifdef CWDEBUG
      label_ = std::move(other.label_);
#endif
    }
    return *this;
  }

  // Start a labeled cooperative thread with callable f.
  //
  // The callable is moved into the new thread and may therefore own move-only captures. It receives the new thread's stop token when invocable with
  // one; otherwise it is invoked without arguments.
  template <typename F>
  [[nodiscard]] static JoinThread create(std::string CWDEBUG_ONLY(label), F&& f)
  {
#ifdef CWDEBUG
    std::string owner_label = label;
    std::jthread thread([label = std::move(label), f = std::forward<F>(f)](std::stop_token st) mutable {
      init_thread(label);
      if constexpr (std::is_invocable_v<F&&, std::stop_token>)
        std::forward<F>(f)(std::move(st));
      else
        std::forward<F>(f)();
    });
    return JoinThread(std::move(thread), std::move(owner_label));
#else
    return JoinThread(std::jthread([f = std::forward<F>(f)](std::stop_token st) mutable {
      if constexpr (std::is_invocable_v<F&&, std::stop_token>)
        std::forward<F>(f)(std::move(st));
      else
        std::forward<F>(f)();
    }));
#endif
  }

  [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
  [[nodiscard]] std::thread::id get_id() const noexcept { return thread_.get_id(); }
  [[nodiscard]] std::stop_source get_stop_source() noexcept { return thread_.get_stop_source(); }
  [[nodiscard]] std::stop_token get_stop_token() const noexcept { return thread_.get_stop_token(); }
  bool request_stop() noexcept { return thread_.request_stop(); }
  [[nodiscard]] std::jthread::native_handle_type native_handle() { return thread_.native_handle(); }

  // Wait for the owned thread to finish after checking the debug no-long-wait contract.
  void join()
  {
    assert_join_allowed("explicitly joining JoinThread");
    thread_.join();
  }

  // Relinquish ownership without waiting. The detached thread keeps its own stop state alive.
  void detach() { thread_.detach(); }

  void swap(JoinThread& other) noexcept
  {
    thread_.swap(other.thread_);
#ifdef CWDEBUG
    label_.swap(other.label_);
#endif
  }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
#ifdef CWDEBUG
  JoinThread(std::jthread thread, std::string label) : thread_(std::move(thread)), label_(std::move(label)) { }
#else
  explicit JoinThread(std::jthread thread) : thread_(std::move(thread)) { }
#endif

  void assert_join_allowed(std::string_view operation) const
  {
#ifdef CWDEBUG
    if (current_thread_holds_long_wait_incompatible_lock())
      DoutFatal(dc::coredump, "Potentially blocking operation while holding (an) incompatible lock(s) [" <<
          detail::long_wait_incompatible_locks << "]: " << operation << " \"" << label_ << "\".");
#else
    static_cast<void>(operation);
#endif
  }

  std::jthread thread_;
#ifdef CWDEBUG
  std::string label_;
#endif
};

// Start a plain (non-cooperative) thread whose first action is init_thread(label).
// Returns a std::thread.
//
// `f` is invoked with no arguments and is moved (never copied) into the thread,
// so move-only captures work. Use this for worker loops that do not participate
// in cooperative cancellation via std::stop_token (e.g. a writer or deadline
// loop that is signaled through its own mutex/condition variable).
template <typename F>
[[nodiscard]] inline std::thread make_thread(std::string CWDEBUG_ONLY(label), F&& f)
{
#ifdef CWDEBUG
  return std::thread([label = std::move(label), f = std::forward<F>(f)]() mutable {
    init_thread(label);
    std::forward<F>(f)();
  });
#else
  return std::thread(std::forward<F>(f));
#endif
}

// Run `f` asynchronously on a new thread (std::launch::async) whose first action
// is init_thread(label), returning a std::future to its result.
//
// `f` is invoked with no arguments; its return value becomes the future's value.
// `f` is moved (never copied). Unlike a raw std::async, this always uses
// std::launch::async (a real new thread) so init_thread always runs on a freshly
// spawned thread.
template <typename F>
[[nodiscard]] inline auto make_async(std::string CWDEBUG_ONLY(label), F&& f) -> std::future<std::invoke_result_t<F&&>>
{
#ifdef CWDEBUG
  return std::async(std::launch::async, [label = std::move(label), f = std::forward<F>(f)]() mutable {
    init_thread(label);
    return std::forward<F>(f)();
  });
#else
  return std::async(std::launch::async, std::forward<F>(f));
#endif
}

}  // namespace ava::core
