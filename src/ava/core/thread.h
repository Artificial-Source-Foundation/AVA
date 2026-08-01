#pragma once

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
//   ava::core::make_jthread("run_rpc_loop", [](std::stop_token stop) { ... });
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

#include <future>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include "debug.h"      // DEBUG_ONLY

namespace ava::core {

#ifdef CWDEBUG
// Register the calling thread with libcwd under `label`, so that debug output
// can be attributed to it and the thread is named.
//
// Returns void. No-op when libcwd (CWDEBUG) is not enabled.
void init_thread(std::string const& label);
#endif

// Start a cooperative, stop-token-aware thread whose very first action is
// init_thread(label). Returns a std::jthread.
//
// `f` is the thread body. This is a drop-in replacement for constructing
// std::jthread directly and preserves std::jthread's automatic stop-token
// forwarding: if `f` is callable with a std::stop_token as its first parameter,
// it is invoked with the thread's stop token; otherwise it is invoked with no
// arguments. `f` is moved (never copied) into the thread, so move-only captures
// work exactly as with a hand-written std::jthread.
//
// The label is mandatory and must be a human-readable name for the thread, e.g.
// "run_rpc_loop" or "acp_writer".
template <typename F>
[[nodiscard]] inline std::jthread make_jthread(std::string CWDEBUG_ONLY(label), F&& f)
{
#ifdef CWDEBUG
  return std::jthread([label = std::move(label), f = std::forward<F>(f)](std::stop_token st) mutable {
    init_thread(label);
    if constexpr (std::is_invocable_v<F&&, std::stop_token>)
      std::forward<F>(f)(std::move(st));
    else
      std::forward<F>(f)();
  });
#else
  if constexpr (std::is_invocable_v<F&&, std::stop_token>)
    std::jthread([f = std::forward<F>(f)](std::stop_token st) mutable {
      std::forward<F>(f)(std::move(st)));
    };
  else
    std::jthread(std::forward<F>(f)());
#endif
}

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
  return std::thread(std::forward<F>(f)());
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
  return std::async(std::launch::async, std::forward<F>(f)());
#endif
}

}  // namespace ava::core
