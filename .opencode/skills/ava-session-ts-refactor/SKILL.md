---
name: ava-session-ts-refactor
description: AVA runtime::Session to runtime::session_ts refactors. Use when fixing one AVA translation unit after an API starts requiring unlocked_session, session_ts, rat, crat, wat, or CRITICAL_AREA_* locking.
---

# AVA `session_ts` Refactor

Convert AVA session call sites carefully, one translation unit and one object target at a time. Stop after the requested target compiles so the user can review before work continues.

## Scope And Verification

1. Modify only the translation unit and any explicitly authorized companion file named by the user.
2. Reproduce errors by building only the requested `.cpp.o` target:

   ```sh
   aap-build --target <requested-object-target> -- --quiet
   ```

3. Do not build broader targets or run tests unless the user explicitly asks.
4. Inspect the scoped diff and run `git diff --check` for authorized files.
5. Stop for review after the object target compiles.

## Core Invariants

- A function that passes `unlocked_PATTERN` (e.g `unlocked_session`) should already own or receive that unlocked wrapper. Never derive a `session_ts&` from a locked `Session&`; that direction is prohibited by design.
- Use `unlocked_PATTERN` only while no guard created from it, i.e. `crat`, `rat`, or `wat` instance, is locked.
- Before passing the unlocked wrapper to a function (or otherwise use it), release the existing guard with `CRITICAL_AREA_END_R(PATTERN)` or `CRITICAL_AREA_END_W(PATTERN)`. Do NOT call `unlock()` or `relock(...)` directly on a guard instance directly (e.g., no `session_w.unlock()`). In situations that an unlock is required you should always use the provided `CRITICAL_AREA_*` macros.
- If a function needs read or write access but never uses the unlocked wrapper, then do not use the `CRITICAL_AREA_*` macros. Simply create a `PATTERN_r` or `PATTERN_w` guard directly (e.g. `ava::app::runtime::session_ts::rat session_r(*unlocked_session_result);`).
- NEVER retain a pointer, reference, `string_view`, iterator, span, or other view into `Session` storage across the end of its critical area. Copy values that must outlive the guard.
- Never hide a `Session&` obtained through an access guard inside a callback capture. If the callback is guaranteed to run while the guard remains locked, capture the guard itself by reference and access the Session through it. This makes the callback's lifetime and locking dependency explicit, and use after `unlock()` fails immediately instead of silently racing through a retained `Session&`.
- Use read access for const operations. Use write access only when mutation is required or an existing API takes `Session&`.
- Treat a temporary access guard as protecting only its full expression. Use a named guard or a critical-area macro when several statements require access.

## Naming And Macros

Choose the semantic pattern name first. Use `session` for AVA's current main interactive session unless another name is genuinely clearer because multiple distinct sessions are in scope.

The macros take `PATTERN`, not `unlocked_PATTERN`:

```cpp
runtime::session_ts& unlocked_session = state.unlocked_session;
CRITICAL_AREA_BEGIN_R(session);  // creates session_r
// Read through session_r.
CRITICAL_AREA_END_R(session);

CRITICAL_AREA_BEGIN_W(session);  // creates session_w
// Read or write through session_w.
CRITICAL_AREA_END_W(session);
```

Use:

- `CRITICAL_AREA_BEGIN_R(PATTERN)` for a mutable `session_ts&` that only needs read access.
- `CRITICAL_AREA_BEGIN_CR(PATTERN)` for a `session_ts const&` that needs read access.
- `CRITICAL_AREA_BEGIN_W(PATTERN)` only for write access.
- `CRITICAL_AREA_CONTINUE_R/W(PATTERN)` only to relock a guard previously released by the matching `END` macro.

## Conversion Workflow

### 1. Identify the required unlocked call

Locate each changed API call and determine whether the enclosing function already has a `session_ts`, `session_ts&`, or struct member containing one.

If the function already has an unlocked wrapper:

1. Introduce `unlocked_PATTERN` near its source if needed.
2. Convert surrounding `Session` accesses to `PATTERN_r` or `PATTERN_w`.
3. End the critical area before passing `unlocked_PATTERN` onward.

Do not merely replace a `Session&` argument with `unlocked_PATTERN` while a guard remains locked; that self-deadlocks when the callee acquires the same mutex.

### 2. Pick the narrowest access scope

For one expression, use a temporary when its lifetime is obvious:

```cpp
auto store = runtime::session_ts::rat(unlocked_session)->permission_rule_store();
```

For multiple statements that create values needed after unlocking, use the macros:

```cpp
CRITICAL_AREA_BEGIN_R(session);
auto provider_id = session_r->model().provider_id;  // owning copy
auto paths = session_r->paths();                    // owning copy if needed later
CRITICAL_AREA_END_R(session);

return run_prompt(unlocked_session, prompt, provider, transport, options);
```

Audit every variable crossing `CRITICAL_AREA_END_*`. It must own its data and must not refer into the protected `Session`.

### 3. Capture access guards or unlocked wrappers, never `Session&`

When a callback is created and consumed entirely inside one critical area, capture the access guard by reference:

```cpp
CRITICAL_AREA_BEGIN_R(session);
options.callback = [&session_r] {
  return session_r->store.session_id();
};
consume_callback_synchronously(options.callback);
CRITICAL_AREA_END_R(session);
```

Do not capture the dereferenced Session:

```cpp
// Wrong: the callback can retain unguarded access after session_r is unlocked.
options.callback = [&session = *session_r] {
  return session.store.session_id();
};
```

If the callback can outlive the critical area or is invoked after the guard is unlocked, end the critical area before creating it. Capture `unlocked_PATTERN` by reference and acquire the narrowest guard inside the callback:

```cpp
CRITICAL_AREA_END_R(session);
options.callback = [&unlocked_session] {
  return runtime::session_ts::rat(unlocked_session)->store.session_id();
};
```

Such a callback must only be invoked when its caller does not already hold a `crat`, `rat`, or `wat` for the same wrapper, or it will self-deadlock.

### 4. Extract successful results early

When a creator returns `Result<session_ts>`, create the conventionally named unlocked variable immediately after checking the result, rather than immediately before its first critical area:

```cpp
auto unlocked_session_result = runtime::Session::open(open_context);
if (!unlocked_session_result)
  return std::unexpected(unlocked_session_result.error());
runtime::session_ts& unlocked_session(*unlocked_session_result);

// Other setup may occur here.
CRITICAL_AREA_BEGIN_W(session);
```

Only do this when `CRITICAL_AREA_*` macros are used (i.e. the code below uses an unlocked wrapper too).

### 5. Convert struct members when necessary

If a caller only has a struct's `Session`, `Session&`, or `Session const&` member, convert that member respectively to the appropriate `session_ts`, `session_ts&`, or `session_ts const&` form. Remove a parallel `session_mutex` member because `session_ts` owns the mutex.

Then update construction and access:

- Accept/pass the unlocked wrapper at the struct boundary.
- Use scoped `crat`, `rat`, or `wat` guards for member access.
- Consider a small accessor returning a guard by value when repeated temporary access is otherwise noisy:

  ```cpp
  [[nodiscard]] runtime::session_ts::crat session_r() const
  {
    return unlocked_session_;
  }
  ```

Do not add such an accessor for a single use.

### 6. Trace signature cascades cautiously

If the struct is initialized where only a locked `Session&` exists, the enclosing function may also need to accept `session_ts&`. Follow callers upward one step at a time.

Use `session_dag.txt` to understand functions under `src/ava` that accept or own sessions, but remember its blind spots:

- tests are not represented;
- callers that own a `Session` member but do not accept one may be absent;
- factories, callbacks, and lambdas may require separate searches.

Do not perform a broad cascade preemptively. Compile and review one translation unit before proceeding.

## Review Checklist

- [ ] Only authorized files changed.
- [ ] The requested object target compiles.
- [ ] Every unlocked-wrapper call occurs outside a locked guard.
- [ ] Every session access occurs through a live `crat`, `rat`, or `wat`.
- [ ] No reference or view into session storage survives an unlock.
- [ ] Callbacks capture an access guard while used inside its critical area, or capture the unlocked wrapper and lock internally when used outside it; none capture a dereferenced `Session&`.
- [ ] Read-only code uses read access.
- [ ] Struct mutexes made redundant by `session_ts` were removed when that struct was converted.
- [ ] No broader build or tests were run without permission.
- [ ] After creating an unlocked alias, for the sake of macros, the code below it no longer uses the source (e.g. `*unlocked_session_result`) - but only uses the alias.
- [ ] Every `CRITICAL_AREA_BEGIN_*` is followed by a `CRITICAL_AREA_END_*` followed by usage of the unlocked wrapper.
