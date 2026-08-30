# ADR: Application-Scoped Process Supervision

- Status: Accepted
- Date: 2026-08-30
- Milestone: Backend modernization M1

## Context

AVA currently has no shared process owner. The [M0 current-state inventory](backend-current-state.md#process-spawn-sites)
finds nine POSIX spawn families, each with local fork, signal, wait, environment, and
cleanup behavior:

| # | Family | Current site | Material lifecycle difference |
| --- | --- | --- | --- |
| 1 | curl transport | [`CurlCliTransport`](../../src/ava/http/curl_transport.cpp) | Per-request PID kill and exact wait, but no descendant group cleanup. |
| 2 | sealed bash | [`run_bash`](../../src/ava/tools/bash_tool.cpp) | Verified leader/sentinel group and descriptor-gated exec; strongest existing path, but locally owned. |
| 3 | plugin | [`PluginProcess`](../../src/ava/plugin/runner.cpp) | One-shot stdio child; natural leader exit can leave a same-group descendant. |
| 4 | MCP | [`McpStdioClient`](../../src/ava/mcp/stdio_client.cpp) | One-shot stdio child with the same natural-exit gap and optional ambient environment inheritance. |
| 5 | LSP | [`LspProcess`](../../src/ava/lsp/lsp_process.cpp) | Run-scoped cached child with a verified group and strict environment. |
| 6 | Mermaid | [`MermaidRenderCoordinator`](../../src/ava/app/mermaid_render_coordinator.cpp) | Descriptor execution, stop handshake, deadline, and locally owned group cleanup. |
| 7 | browser opener | [`open_url_in_browser`](../../src/ava/app/browser_open.cpp) | Double-forks, calls `setsid`, and deliberately leaves an unowned grandchild. |
| 8 | clipboard helper | [`capture_command_stdout`](../../src/ava/app/clipboard_image.cpp) | Bounded output/deadline, but PID-only termination. |
| 9 | external editor | [`edit_text_with_external_editor`](../../src/ava/app/external_editor.cpp) | `std::system` blocks and reaps, without explicit process-group or terminal ownership. |

The distributed implementation permits ownership gaps, duplicated race handling,
inconsistent child environments, and shutdown time proportional to the number of
children. M1 needs one lifecycle contract without weakening the sealed-command
boundary, changing extension protocols, or introducing a framework dependency.

## Decision

### Module and composition boundary

Add a dependency-low backend module at `src/ava/process/`, built as `ava_process`
and exposed internally as `AVA::process`. It owns process reservation, launch/adoption,
monitoring, signaling, group cleanup, reaping, and content-free lifecycle snapshots.
It depends only on `AVA::core`, the C++ standard library, and operating-system APIs;
it does not depend on app, agent, command, diagnostics, HTTP, tools, plugin, MCP,
LSP, TUI, or protocol code, and adds no third-party production dependency.

The application composition root constructs exactly one `ava::process::Supervisor`
for one AVA invocation. A reference or shared application-lifetime capability is
passed explicitly through constructors/options to every process-using subsystem.
There is no `Supervisor::instance()`, function-local service, service locator, hidden
global, or lookup through [`core::Application::instance`](../../src/ava/core/Application.h).
Tests inject an explicitly owned supervisor or test implementation through the same
narrow capability. Consumers are destroyed before the supervisor; the composition
root then calls its bounded shutdown.

### Versioned ownership and closed vocabulary

`OwnerPathV1` is a typed, version-1 hierarchy:

```text
application/<opaque-id>
  [/session/<opaque-id>]
  [/run/<opaque-id>]
  /operation/<opaque-id>
```

Application and operation are required for a launch; run requires session.
Application-owned operations such as browser opening omit session and run. Segments
are generated opaque IDs, not user labels, and are bounded to 64 bytes with a fixed
maximum depth of four. Stopping an owner stops every descendant owner, so run,
session, and application shutdown use one mechanism. An owner path is internal
lifecycle identity, not a session or wire-protocol field.

The following version-1 values are closed; unknown serialized values fail validation,
and additions require a versioned schema change rather than arbitrary strings:

- `ProcessRoleV1`: `curl`, `bash`, `plugin`, `mcp`, `lsp`, `mermaid`,
  `browser_opener`, `clipboard_helper`, `external_editor`.
- `ProcessStateV1`: `reserved`, `launching`, `running`, `stop_requested`,
  `reaping`, `finished`.
- `TerminationReasonV1`: `natural_exit`, `launch_failed`, `exec_failed`,
  `canceled`, `deadline_expired`, `owner_shutdown`, `application_shutdown`,
  `output_limit`, `protocol_failure`, `unsupported_suspension`.
- `ChildMemberV1`: `leader`, `sentinel`. A sentinel is registered only by the
  sealed-bash adoption path.

The first reason committed under the supervisor lock is immutable. Numeric exit code
or signal is a separate bounded status field; it does not create an open-ended reason.

### API and authority contract

The narrow API consists of the following operations; concrete C++ names may vary only
if they preserve these ownership transitions:

```text
reserve(owner, role, lifecycle_policy) -> Reservation
spawn(Reservation, SpawnSpec) -> ProcessHandle
begin_secure_adoption(Reservation) -> AdoptionGate
adopt(AdoptionGate, exact_child_members) -> ProcessHandle
request_stop(ProcessHandle | owner-prefix, TerminationReasonV1, absolute_deadline)
wait(ProcessHandle, absolute_deadline) -> ExitStatusV1
snapshot() -> ProcessSnapshotV1
shutdown(absolute_deadline) -> ShutdownResultV1
```

`Reservation`, `AdoptionGate`, and `ProcessHandle` are opaque RAII capabilities and
do not expose signal or reap authority. Reservation consumes live-record capacity
**before any fork**. Capacity exhaustion, invalid owners/specifications, pipe setup,
executable resolution, environment construction, and descriptor preparation therefore
fail without creating a child. A reservation abandoned before fork releases capacity.

For both launch paths, argv, environment, cwd, descriptor actions, signal defaults,
and the exec-error channel are prepared before fork. The child creates a private group
with `PGID == leader PID` and blocks behind a close-on-exec gate. Before releasing it,
the parent repeats `setpgid(child, child)`, verifies `getpgid(child) == child`, verifies
the group differs from AVA's group, and commits the exact child identity to the
supervisor. Failure keeps the gate closed and transfers the child immediately to
supervisor cleanup. No child may exec while unreserved or unregistered.

`spawn` owns the complete common fork/gate/exec sequence. Secure adoption is not a
public “adopt any PID” escape hatch: the caller must obtain its reservation and gate
before its custom fork, the child may perform only the reviewed async-signal-safe
pre-exec sequence, and the supervisor assumes signal/wait/reap authority as soon as
the parent has the PID, including every adoption-failure path.

After fork, the supervisor is the **only** code allowed to call `kill`, `killpg`,
`waitid`, or `waitpid` for a managed child. Callers request a reason and deadline and
observe the handle. The POSIX backend observes a leader with exact
`waitid(P_PID, ..., WNOWAIT)` semantics, keeping the waitable leader unreaped so its
PID/PGID cannot be recycled. Even when the leader exits naturally with status zero,
the supervisor performs group cleanup before exact reaping. It sends graceful and
escalation signals only to a launch-verified private PGID and keeps a waitable leader
unreaped while signaling/waiting so the PID/PGID identity cannot be recycled. It then
calls `waitpid` for each registered direct child PID (leader and optional sentinel);
group liveness checks must account for the retained zombie and never signal after that
identity is released. It never uses `waitpid(-1)`, `waitpid(0)`, a
process-wide subreaper, or a global `SIGCHLD` handler, so unrelated children cannot be
stolen. An unverified group is never group-signaled; it fails before exec and is
terminated/reaped by exact PID.

The table fixes which launch surface each family adopts:

| Role | M1 integration | Subsystem behavior retained outside the supervisor |
| --- | --- | --- |
| curl | Common `spawn` | curl config generation, request body, response parsing/cap, streaming, and cancellation polling. |
| bash | Secure adoption | Sealed plan, executable/interpreter descriptors, containment handshake, synthetic environment, and sentinel purpose. |
| plugin | Common `spawn` | JSONL protocol, stderr/output bounds, permissions, startup/request deadlines. |
| MCP | Common `spawn` | JSON-RPC framing, permissions, explicit config, startup/request deadlines. |
| LSP | Common `spawn` with a pre-opened executable descriptor when required | Executable identity revalidation, LSP framing/cache, request cancellation. |
| Mermaid | Secure adoption | `O_NOFOLLOW` executable descriptor, renderer protocol, existing child-side stop/exec handshake, output validation. |
| browser opener | Common `spawn`, direct child | URL/command allowlist and `/dev/null` stdio. No double-fork or `setsid`. |
| clipboard helper | Common `spawn` | Helper selection, output byte cap, MIME handling, and read deadline. |
| external editor | Common `spawn` plus terminal lease | Editor selection, private draft file, shell-compatible command semantics, draft cap. |

### Bounds, monitor, and shutdown

The application policy permits at most 256 live/reserved records and retains at most
256 finished content-free records. A finished record is evicted FIFO; process handles
retain only their final value, not an unbounded history. The single monitor is created
lazily on the first successful fork, not at application startup, and uses exact known
children rather than one thread per child. It is woken by reservations, stop requests,
and deadline changes and remains idle without busy polling.

Normal operation deadlines are absolute `steady_clock` values; no layer resets a
relative timeout after progress. Application shutdown has one two-second monotonic
budget shared by all records: request graceful stop for all groups in one sweep, wait
within the common deadline, escalate all remaining verified groups in one sweep, and
perform exact nonblocking reap attempts until that same deadline. It is never a fresh
grace period per child. `ShutdownResultV1` reports a bounded incomplete count if the
budget expires; destructors are no-throw and do not add an unbounded wait.

### Platform guarantee and non-guarantees

On supported Linux and conservative POSIX builds, M1 guarantees cleanup of the leader,
registered sentinel, and every descendant that remains in the launch-verified private
process group, including after natural leader exit, cancellation, timeout, owner
shutdown, and normal application shutdown. “Managed process tree” means this
managed-group scope; it is not an OS sandbox. Linux may use a stronger observation
primitive internally, but the semantic floor is the exact-PID POSIX contract above.

M1 explicitly does **not** guarantee cleanup of a descendant that escapes with
`setsid` or moves to another group, a process stuck so that even `SIGKILL` cannot make
it waitable within the budget, or any child after AVA itself is killed/crashes before
cleanup. It does not claim cgroup containment, PID-namespace containment, daemon
supervision, or recovery by an external service. It does not claim Windows support.
These limits align with the separate [containment contract](../security/containment.md),
which is not expanded by this ADR.

A future Windows backend must preserve reservation-before-create and sole handle/wait
authority by using `CreateProcessW(..., CREATE_SUSPENDED, ...)`, assigning the process
to a kill-on-close Job Object before `ResumeThread`, and failing closed if assignment
or verification fails. It must use exact process/job handles and a shared shutdown
budget. That future contract is design guidance only: no Windows lifecycle or tree
cleanup support is advertised until its implementation and descendant/timeout/shutdown
tests pass on Windows.

### Diagnostics, output, and deadline ownership

Supervisor snapshots are private, bounded, and content-free. A record may contain only
schema version, aliased owner identity, closed role/state/reason, booleans such as
`group_verified`, monotonic duration, numeric exit/signal class, and aggregate counts.
It must never contain argv or shell text, executable/cwd/file paths, URL, plugin/MCP/LSP
name, PID/PGID in a persisted artifact, environment names or values, credentials,
stdin/stdout/stderr, protocol frames, prompts, or tool content. Raw owner and PID data
remain transient in-process. If adapted into the existing [private trace](../operations/diagnostics.md#private-runtime-trace),
owner IDs use its per-trace aliases and support export receives aggregate typed counts
only. No process record is written to sessions, provider context, RPC/ACP, or ordinary
user output.

The spawning subsystem owns pipe draining, byte/record caps, decoding, protocol
progress, and user-visible result mapping. The supervisor neither buffers nor logs
child output. On output cap or protocol failure the subsystem closes/drains according
to its protocol and requests supervisor stop with the matching closed reason.
Subsystems own request/protocol/output deadlines and pass absolute deadlines; the
supervisor owns launch-gate, termination-grace, owner-shutdown, and application-shutdown
deadlines. The earlier absolute deadline wins, and cleanup never extends it by starting
a new relative timeout.

### Foreground editor and browser opener

External-editor launch acquires an RAII foreground-terminal lease after the editor's
private PGID is verified and before its exec gate opens. The lease opens the controlling
terminal, records AVA's foreground PGID and termios state, temporarily blocks `SIGTTOU`
while calling `tcsetpgrp` for the editor group, and restores AVA's PGID, termios, and
signal mask on every normal, nonzero, signaled, canceled, launch-failure, and shutdown
path. Failure to acquire or transfer the terminal keeps the exec gate closed and fails
the launch safely.

M1 does not implement a shell-style stopped-job UI. If an editor stops, the monitor
reports `unsupported_suspension`, the lease immediately restores AVA to the foreground,
and the supervisor sends `SIGCONT` followed by bounded normal escalation within the
existing deadline; it never waits forever on a suspended editor. PTY tests must prove
that the editor observes itself in the foreground and that AVA regains the terminal
and original termios after normal exit, signal, transfer failure, and suspension.

Browser opening launches the selected opener (`$BROWSER`, `xdg-open`, `gio open`,
`open`, or `wslview`) once as a direct supervised child with `/dev/null` stdio and a
10-second opener deadline. Success means the exec handshake succeeded; monitoring then
continues under the application owner without blocking the OAuth flow. M1 removes the
double-fork/`setsid` path. If an opener later daemonizes or calls `setsid`, that escaped
process is outside the stated managed-group guarantee.

### Exact child environment profiles

Every environment is constructed and bounded in the parent, rejects duplicate names or
NULs, and is passed with `execve`/descriptor exec; child-side `setenv` and ambient
`execvp` lookup are removed. The trusted executable path is
`/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin` unless a row says
otherwise. “Inherit” always means only the names listed, once each, if present.

| Role / profile ID | Exact child environment |
| --- | --- |
| curl / `ava-curl-v1` | Fixed `PATH` (trusted path), `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, `PWD=/`; inherit only `http_proxy`, `https_proxy`, `ftp_proxy`, `all_proxy`, `no_proxy`, `HTTP_PROXY`, `HTTPS_PROXY`, `FTP_PROXY`, `ALL_PROXY`, `NO_PROXY`, `CURL_CA_BUNDLE`, `SSL_CERT_FILE`, and `SSL_CERT_DIR`. |
| bash / existing `ava-local-bash-prompt-v2` | Preserve the sealed environment from [`EnvironmentFactory`](../../src/ava/command/environment.cpp): `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, `LC_CTYPE=C.UTF-8`, `TZ=UTC`, bounded `USER` and `LOGNAME`, logical `PWD`, sealed `PATH`, and AVA-created private `HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, and `TMPDIR`. No ambient additions. |
| plugin / `ava-plugin-minimal-v1` | Fixed trusted `PATH`, `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, and computed `PWD`; no inherited variables, ambient proxy/CA, provider/cloud credentials, tokens, or arbitrary `AVA_*`. Plugin v1 has no environment override. |
| MCP / `ava-mcp-explicit-v1` | Supervisor-computed `PWD`; trusted `PATH` unless the bounded explicit server config supplies `PATH`; otherwise exactly the bounded explicit `env` pairs and nothing inherited. `PWD` is reserved, duplicate names fail, and all launches behave as `clean_environment=true`. Explicitly configured secrets remain an intentional user grant, never ambient inheritance. |
| LSP / `ava-lsp-strict-v1` | Preserve [`lsp_environment`](../../src/ava/lsp/lsp_process.cpp): fixed trusted `PATH`, computed `PWD`, and only inherited `HOME`, `USER`, `LOGNAME`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, every other `LC_*`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `TERM`, and `COLORTERM`. |
| Mermaid / `ava-mermaid-v1` | Preserve the exact fixed set: `PATH=/usr/local/bin:/usr/bin:/bin`, `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, `TERM=dumb`, `NO_COLOR=1`, `PWD=/`, `AVA_MERMAID_PROTOCOL=1`. |
| browser opener / `ava-browser-desktop-v1` | Bounded inherited `PATH`; inherit only `HOME`, `USER`, `LOGNAME`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, `DISPLAY`, `WAYLAND_DISPLAY`, `XAUTHORITY`, `DBUS_SESSION_BUS_ADDRESS`, `DESKTOP_STARTUP_ID`. `$BROWSER` is resolved by the parent and not forwarded. |
| clipboard helper / `ava-clipboard-desktop-v1` | Fixed trusted `PATH`; inherit only `HOME`, `USER`, `LOGNAME`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `XDG_RUNTIME_DIR`, `DISPLAY`, `WAYLAND_DISPLAY`, `XAUTHORITY`, and `DBUS_SESSION_BUS_ADDRESS`. AVA clipboard test/selection variables are parent-only. |
| external editor / `ava-external-editor-v1` | Bounded inherited `PATH`; inherit only `HOME`, `USER`, `LOGNAME`, `SHELL`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `TERM`, `COLORTERM`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, `DISPLAY`, `WAYLAND_DISPLAY`, `XAUTHORITY`, and `DBUS_SESSION_BUS_ADDRESS`; add only the supervisor-created `AVA_EXTERNAL_EDITOR_FILE`. `VISUAL`/`EDITOR` are resolved by the parent and not forwarded. |

Only curl receives ambient proxy or CA variables. An MCP server receives such a value
only when its explicit configuration names it. No other profile receives ambient proxy,
CA, provider, cloud, API-key, token, secret, password, credential, or arbitrary `AVA_*`
variables. This deliberately tightens the current plugin and legacy global/project MCP
environments; it must receive canary-based compatibility tests and release-note/user-doc
updates when implementation activates.

### Dependency gate

Before the first caller migrates, the module policy checker
[`tests/module_dependency_rules_test.py`](../../tests/module_dependency_rules_test.py)
must recognize `process`: `process` may include only `core`, while each spawning module
may depend downward on `process`. The policy fixture
[`module_dependency_rules.json`](../../tests/fixtures/module_dependency_rules.json)
must remain `{"exceptions":[]}`. Any proposed exception, reverse dependency, cycle,
or new production package blocks M1; there is no temporary exception path for this
module.

## Migration and verification

Migration is incremental, with exactly one lifecycle authority per family at every
commit:

1. **Foundation:** add `AVA::process`, closed types, explicit app-scoped injection,
   fake child helpers, the zero-exception dependency rule, and inert unit/integration
   coverage. No production family migrates in this wave.
2. **Common noninteractive spawn:** migrate curl and clipboard; prove output/deadline
   behavior and environment canaries before removing their local wait/kill helpers.
3. **Protocol children:** migrate plugin, MCP, and LSP without changing their wire
   protocols, permissions, output limits, or one-shot/run-scoped compatibility.
4. **Secure adoption:** migrate bash and Mermaid while preserving descriptor identity,
   containment, sentinel, stop handshake, and strict environments. Remove duplicated
   process-group code only after parity tests pass.
5. **Foreground/application helpers:** migrate external editor and browser, run PTY and
   OAuth opener tests, then statically prove all nine production families use the
   supervisor and no production `fork`/wait/signal owner remains outside the two
   reviewed module backends.

Required tests cover owner-prefix cancellation; reservation exhaustion before fork;
exec-gate and PGID-verification failures; exact-PID reaping without stealing an
unrelated child; normal/nonzero/signaled exits; natural leader exit with a surviving
same-group descendant; descendant TERM refusal and escalation; cancellation, timeout,
output limit, protocol failure, and concurrent application shutdown; record eviction;
lazy-monitor startup; every environment allowlist with credential/proxy/CA canaries;
bash sentinel and containment parity; LSP/Mermaid executable identity; browser direct
parentage; and the external-editor PTY cases above. Leak tests inspect descendants after
each case and disclose the documented `setsid` escape rather than claiming containment.
Linux runs the strong-path suite; another supported POSIX CI lane runs the conservative
contract. Windows tests are deferred with Windows support.

Benchmarks extend the [M0 methodology and baseline](performance-baseline.md) with idle
startup/RSS (proving no eager monitor), first-spawn latency, warm sequential spawn,
1/8/64 concurrent records, natural-leader descendant cleanup, and shared-budget
shutdown. Compare curl, plugin, MCP, LSP, and bash before/after on the same host/build;
report samples and distributions without converting unsupported seams into claims.
The focused process suite, module dependency gate, complete CTest suite, sanitizer runs
as supported, Markdown gates, and `git diff --check` are M1 acceptance evidence.

### Compatibility and rollback

Owner IDs, roles, states, reasons, and snapshots are internal and do not alter
[RPC](../rpc-protocol.md), [headless](../headless-protocol.md),
[ACP](../acp.md), session, plugin `ava.plugin.v1`, MCP `2024-11-05`, or LSP protocol
formats. Subsystems retain result mapping, output caps, permission prompts, and existing
request deadlines. Sealed bash containment and strict bash/LSP/Mermaid environments
must remain byte-for-byte/profile-equivalent where stated.

Intentional compatibility changes are group cleanup after natural leader exit, removal
of ambient plugin/MCP environment inheritance, supervised direct browser opening, and
bounded failure of suspended external editors. They are activation notes, not silent
fallbacks.

Each migration wave is a separately revertible commit. During transition, construction
selects exactly one owner; old and new code must never wait or signal the same PID.
There is no retry through the legacy path after fork or after the exec gate opens,
because that could duplicate a request or tool side effect. If a wave regresses its
compatibility tests or benchmark investigation budget, revert that whole family wave
to the last passing authority, keep the inert supervisor foundation, and record the
blocker in the program ledger. M1 is not complete until local ownership code is removed
for all nine families; runtime dual-path flags are not a permanent rollback mechanism.

## Planning gate resolutions

### M1-GATE-001

**Resolved — lifecycle and platform scope.** M1 guarantees exact-child and
natural-leader-exit cleanup for launch-verified Linux/POSIX managed groups. It rejects
`setsid`, cgroup, crash-recovery, and Windows containment claims; Windows remains the
suspended-`CreateProcessW` plus Job Object future contract described above.

### M1-GATE-002

**Resolved — composition and adoption authority.** Use one explicitly injected,
application-scoped, dependency-low `AVA::process` supervisor. Reservation precedes
fork; common spawn or pre-gated secure adoption transfers sole signal/wait/reap
authority. No singleton, arbitrary PID adoption, module-cycle exception, or new
production dependency is accepted.

### M1-GATE-003

**Resolved — exceptional families, privacy, and compatibility.** The exact environment
profiles, content-free private diagnostics, subsystem output/deadline ownership,
foreground-terminal RAII with bounded unsupported suspension, and direct supervised
browser opener are fixed by this ADR and must pass the named canary/PTTY/migration
tests before activation.

These gates resolve design blockers only. They do not assert that the supervisor or any
migration is already implemented.

## Consequences

One application owner can now provide deterministic prefix cancellation, natural-exit
descendant cleanup, bounded diagnostics, and shutdown work that is parallel rather
than per-child cumulative. Process races and environment policy become testable once,
and later persistent plugin work can reuse the same owner/handle contract.

The cost is a new low-level state machine, one lazy monitor after first use, explicit
capability plumbing through current constructors, stricter plugin/MCP compatibility,
and PTY/platform-specific tests. Process groups remain weaker than containment; escaped
or unkillable descendants and AVA crashes remain honestly outside the guarantee.
