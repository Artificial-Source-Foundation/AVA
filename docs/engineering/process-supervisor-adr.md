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
mint_exact_environment(role, profile, profile_inputs) -> ExactEnvironment
mint_preopened_executable(startup_anchor_set, logical_executable,
                          expected_identity) -> PreopenedExecutableV1
mint_anchored_working_directory(startup_anchor_set, logical_cwd,
                                expected_identity) -> AnchoredWorkingDirectoryV1
reserve(owner, role, lifecycle_policy) -> Reservation
spawn(Reservation, SpawnSpec{ExactEnvironment, executable, cwd,
                             optional anchored capabilities, ...}) -> ProcessHandle
begin_secure_adoption(Reservation, ExactEnvironment,
                      AnchoredWorkingDirectoryV1) -> AdoptionGate
adopt(AdoptionGate, exact_child_members) -> ProcessHandle
request_stop(ProcessHandle | owner-prefix, TerminationReasonV1, absolute_deadline)
wait(ProcessHandle, absolute_deadline) -> ExitStatusV1
snapshot() -> ProcessSnapshotV1
shutdown(absolute_deadline) -> ShutdownResultV1
```

`ava_process` is the only minter of the opaque, immutable `ExactEnvironment`
capability. Each capability is bound to exactly one closed role/profile pair. A
`SpawnSpec` or secure-adoption gate consumes that capability; neither can carry a raw
environment, request ambient inheritance, or select a different profile. The process
layer validates the complete environment when minting, then revalidates its bounds and
reservation role/profile at the final pre-fork check. Higher modules remain responsible
for proving facts the neutral process layer cannot establish: the sealed-command
digest, selection of explicit MCP configuration, LSP executable identity, and
parent-side resolution of `BROWSER`, `VISUAL`, and `EDITOR`. They establish those facts
before requesting the capability; `ava_process` does not gain reverse dependencies to
inspect them.

`ava_process` is also the sole minter of move-only, opaque RAII
`PreopenedExecutableV1` and `AnchoredWorkingDirectoryV1` capabilities. Neither exposes
an fd or other native handle, an expected or observed identity, or a path accessor. The
public expected-identity value is ingress-only, process-neutral metadata supplied by a
higher module: exactly `uid`, `gid`, `mode`, `nlink`, `dev`, `inode`, `size`, and ctime
seconds and nanoseconds. It contains no command-, LSP-, Mermaid-, recipe-, permission-,
or provenance-specific field.

Each successful factory result privately retains the supplied shared startup
`AnchorSet` authority, an owned target descriptor, the caller's logical spelling, and
enough parent/final-route descriptor identity to perform one immediate final pre-fork
freshness check. It never reconstructs startup authority from a path. Factory and
freshness errors are content-free: they may identify a bounded operation/stage and
error category or errno class, but never echo the logical spelling, expected or
observed tuple, descriptor number, or native handle. Supervisor snapshots gain none of
those fields and remain content-free under the diagnostics contract below.

Both factories require the logical identity to be nonempty, absolute, NUL-free, and
already lexically normalized. Executable minting requires a regular file with exactly
one link, at least one execute bit, and no group/other write bit. Working-directory
minting requires a directory. For either capability, the route observation before the
open, the opened descriptor identity, and the route observation after the open must be
equal and must exactly match every field of the caller's expected tuple. At the final
pre-fork boundary, the retained parent/final route must still identify that same opened
target. These checks establish process-neutral freshness only; they do not infer why a
target is trusted.

Accordingly, higher LSP code continues to own install-root selection, root/current-user
ownership policy, ancestor safety, workspace exclusion/selection, and ELF recipe
provenance. The command layer continues to own the sealed plan, bounded shebang chain,
and containment decision and plan. The app layer continues to own Mermaid
configured-helper provenance. Those layers reduce their richer evidence to the neutral
expected tuple at ingress; `ava_process` neither accepts their types nor gains a reverse
dependency on them.

As a prerequisite, external `core::AnchorOpen` final-target reopen privately replaces
its `/proc/self/fd` path with
`openat(parent_fd, final_component, requested_flags | O_NOFOLLOW | O_CLOEXEC)` from the
already-held resolved parent descriptor, followed by identity comparison with the
inspected object. This is an internal core hardening, not a new public path or handle
surface. It preserves the existing external-symlink policy and writable-anchor
exclusion. It adds no dependency and no core-to-process reverse edge: `ava_process`
continues to depend downward only on `AVA::core`.

`Reservation`, `AdoptionGate`, and `ProcessHandle` are opaque RAII capabilities and
do not expose signal, reap, PID/PGID, pidfd, or terminal authority. Reservation
consumes live-record capacity **before any fork**. Capacity exhaustion, invalid
owners/specifications, launch-surface mismatch, pipe setup, executable resolution,
exact-environment minting, anchored-capability validation, and descriptor preparation
therefore fail without creating a child. A reservation abandoned before fork releases
capacity.

`LifecyclePolicyV1` may contain an optional absolute `execution_deadline`. The value is
immutable once reserved and is owned by the monitor rather than by a per-launch timer.
At reservation the supervisor also fixes an overflow-safe cleanup horizon two seconds
after that trigger; neither instant is reset by output, protocol progress, or retries. A
deadline already past is rejected before fork, and the final pre-fork check catches a
deadline that elapsed after reservation. If it becomes due after fork but before gate
release, the monitor commits `deadline_expired` unless an earlier reason already won;
in all cases it keeps the gate closed. If it becomes due after release, the monitor
commits the same reason under the same first-reason rule, then cleans and exactly reaps
the group within the reservation-time cleanup horizon. An earlier caller stop or
application-shutdown deadline still shortens that horizon.

For both launch paths, argv, the exact environment, cwd, descriptor actions, signal
defaults, and the child-status channel are prepared before fork. After all fallible
preparation, the process layer performs anchored route freshness immediately before
the existing final role/profile/deadline decision and fork; this is the
namespace-replacement linearization point. A replacement that wins before this check
fails closed and creates neither a child nor a monitor. Once fork inherits descriptor
A, replacing its pathname with object B cannot redirect that child from A to B. A failed
freshness check or descriptor exec is never retried through a pathname. This
replacement guarantee applies only when the corresponding anchored capability is
supplied; path-only launch retains its current pathname semantics.

The child creates a private group with `PGID == leader PID` and blocks behind a
close-on-exec gate. Before releasing it, the parent repeats `setpgid(child, child)`,
verifies `getpgid(child) == child`, verifies the group differs from AVA's group, and
commits the exact child identity to the supervisor. Failure keeps the gate closed and
transfers the child immediately to supervisor cleanup. No child may exec while
unreserved or unregistered.

The child-status channel has closed, content-free framing. A pre-exec setup failure or
EOF before the typed `ExecAttempt` frame commits `launch_failed`; immediately before
the actual exec syscall the child emits `ExecAttempt`, and a syscall that returns emits
the typed `ExecFailed` frame. EOF after `ExecAttempt` with no failure frame confirms
exec. Both failure reasons report the closed exit kind `launch_error`, while retaining
their distinct reasons. An earlier cancellation or owner/application shutdown remains
the first reason and is not overwritten by later launch framing.

Common descriptor execution uses that existing framing and invokes `execveat` with
`AT_EMPTY_PATH` where available or `fexecve` on the conservative POSIX path. It
intentionally does not inspect or resolve a descriptor's shebang. In particular, a
script on a `CLOEXEC` descriptor may return `ENOENT`; that return is one typed
`ExecFailed`, never a request to resolve an interpreter or retry by pathname. Bash
remains separate: its higher command authority keeps the sealed interpreter chain and
the reviewed retained script FDs through secure adoption.

`SpawnSpec` may independently consume a `PreopenedExecutableV1`, an
`AnchoredWorkingDirectoryV1`, both, or neither. When either is present, the public
logical executable or cwd string must exactly match the capability's private logical
spelling; mismatch fails before fork. The corresponding descriptor is then the only
launch authority, with no pathname resolution, open, or fallback. When a capability is
absent, current path-based executable or cwd preparation remains available for roles
and higher-level recipes that do not require descriptor identity.

Secure adoption always consumes an `AnchoredWorkingDirectoryV1`; the spec's cwd string
remains only for private logical and exact-environment matching and is never opened as
a temporary adoption seam. That path-open seam must be removed before either secure
caller migrates. Bash and Mermaid continue to own their already-reviewed executable
descriptors and pass them only through the secure child API; adoption does not transfer
those executable descriptors into a public process capability.

`spawn` owns the complete common fork/gate/exec sequence. Secure adoption is not a
public “adopt any PID” escape hatch: the caller must obtain its reservation, exact
environment, anchored cwd, and gate before its custom fork; the child may perform only
the reviewed async-signal-safe pre-exec sequence, and the supervisor assumes
signal/wait/reap authority as soon as the parent has the PID, including every
adoption-failure path.

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
identity is released. It never uses `waitpid(-1)`, `waitpid(0)`, a process-wide
subreaper, or a global `SIGCHLD` handler, so unrelated children cannot be stolen. An
unverified group is never group-signaled; it fails before exec and is terminated/reaped
by exact PID.

The table fixes which launch surface each family adopts:

| Role | M1 integration | Subsystem behavior retained outside the supervisor |
| --- | --- | --- |
| curl | Common `spawn` | curl config generation, request body, response parsing/cap, streaming, and cancellation polling. |
| bash | Secure adoption with required `AnchoredWorkingDirectoryV1` | Sealed plan, caller-owned executable/interpreter descriptors and retained script FDs, containment handshake, synthetic environment, and sentinel purpose. |
| plugin | Common `spawn` | JSONL protocol, stderr/output bounds, permissions, startup/request deadlines. |
| MCP | Common `spawn` | JSON-RPC framing, permissions, explicit config, startup/request deadlines. |
| LSP | Common `spawn` consuming `PreopenedExecutableV1` when descriptor identity is required | Install/root/user/ancestor/workspace/ELF provenance, LSP framing/cache, request cancellation. |
| Mermaid | Secure adoption with required `AnchoredWorkingDirectoryV1` | Caller-owned `O_NOFOLLOW` executable descriptor, configured-helper provenance, renderer protocol, existing child-side stop/exec handshake, output validation. |
| browser opener | Common `spawn`, direct child | URL/command allowlist and `/dev/null` stdio. No double-fork or `setsid`. |
| clipboard helper | Common `spawn` | Helper selection, output byte cap, MIME handling, and read deadline. |
| external editor | Common `spawn` plus terminal lease | Editor selection; app-owned private draft creation, reading, and removal; shell-compatible command semantics; draft cap. |

This role/surface matrix is closed and checked before any fork. Common `spawn` rejects
bash and Mermaid reservations; secure adoption rejects every role except bash and
Mermaid and rejects a missing anchored cwd. Only bash may request a sentinel. Mermaid's
one expected startup stop is consumed as part of its secure launch protocol before
ordinary suspension handling, so it is not reported as `unsupported_suspension`; a
later unexpected stop receives the normal closed-role handling. There is no generic
sentinel or expected-stop escape hatch.

### Bounds, monitor, and shutdown

The application policy permits at most 256 live/reserved records and retains at most
256 finished content-free records. A finished record is evicted FIFO; process handles
retain only their final value, not an unbounded history. The single monitor is created
lazily on the first successful fork, not at application startup, and uses exact known
children rather than one thread per child. It owns every reserved execution deadline;
there is no deadline helper thread and no relative-timeout reset.

On Linux, pidfds are readiness sources only. The monitor waits on them plus one
supervisor-wide, lazily created `CLOEXEC` wake descriptor; exact
`waitid(P_PID, ..., WNOWAIT)` observation, exact `waitpid(exact_pid)` reaping, and
signaling of only a verified private group remain authoritative. Pidfds and all native
process identifiers remain private implementation details. This path does not use a
pidfd as signal or reap authority.

If pidfds are unavailable, and on the conservative POSIX backend, nonblocking exact-PID
probes start at 10 ms and back off exponentially to at most 1 second. The backoff resets
and the monitor wakes for an active waiter, a stop request, child registration, or a
change to the nearest absolute deadline; deadline waits target that exact instant. On
every backend, an active external-editor terminal lease retains short stopped-job
probes so suspension cannot strand the terminal. These event-driven/adaptive rules are
what “idle without busy polling” means: there is no fixed-rate idle spin, global
`SIGCHLD` handler, subreaper, or `waitpid(-1)` fallback.

Normal operation deadlines are absolute `steady_clock` values; no layer resets a
relative timeout after progress. Expiry before gate release keeps the gate closed;
expiry while running commits `deadline_expired` at the exact trigger, clips graceful
termination to the fixed cleanup horizon, and reserves time for group proof and exact
reaping. A natural exit or earlier caller cancellation keeps its own bounded cleanup
deadline instead of being truncated merely because the execution trigger later passes.
If the reservation-time cleanup horizon cannot complete cleanup, the result is
incomplete rather than silently starting another budget. Application shutdown has one
two-second monotonic budget shared by all records: request graceful stop for all groups
in one sweep, wait within the common deadline, escalate all remaining verified groups
in one sweep, and perform exact nonblocking reap attempts until that same deadline. It
is never a fresh grace period per child. `ShutdownResultV1` reports a bounded incomplete
count if the budget expires; destructors are no-throw and do not add an unbounded wait.

### Platform guarantee and non-guarantees

On supported Linux and conservative POSIX builds, M1 guarantees cleanup of the leader,
registered sentinel, and every descendant that remains in the launch-verified private
process group, including after natural leader exit, cancellation, timeout, owner
shutdown, and normal application shutdown. “Managed process tree” means this
managed-group scope; it is not an OS sandbox. Linux uses an available pidfd only as the
readiness optimization described above; the semantic floor is the exact-PID POSIX
contract.

M1 explicitly does **not** guarantee cleanup of a descendant that escapes with
`setsid` or moves to another group, a process stuck so that even `SIGKILL` cannot make
it waitable within the budget, or any child after AVA itself is killed/crashes before
cleanup. It does not claim cgroup containment, PID-namespace containment, daemon
supervision, or recovery by an external service. It does not claim Windows support.
These limits align with the separate [containment contract](../security/containment.md),
which is not expanded by this ADR.

The `PreopenedExecutableV1` and `AnchoredWorkingDirectoryV1` factories and descriptor
execution have no Windows implementation or support claim. A future Windows backend
must preserve reservation-before-create and sole handle/wait authority by using
`CreateProcessW(..., CREATE_SUSPENDED, ...)`, assigning the process to a kill-on-close
Job Object before `ResumeThread`, and failing closed if assignment or verification
fails. It must use exact process/job handles and a shared shutdown budget; equivalent
handle-backed factories and descriptor-bound image selection would require their own
reviewed design and tests. All of this remains unsupported design guidance only: no
Windows factory, descriptor-execution, lifecycle, or tree-cleanup support is advertised
until the corresponding implementation and tests pass on Windows.

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
Subsystems select request/protocol/output deadlines and the optional absolute execution
deadline carried by `LifecyclePolicyV1`; after reservation the monitor owns its
enforcement. The supervisor also owns launch-gate, termination-grace,
owner-shutdown, and application-shutdown deadlines. The earlier absolute deadline wins,
and cleanup never extends it by starting a new relative timeout.

### Foreground editor and browser opener

The app/editor subsystem, not the supervisor, creates and owns the external editor's
private draft with mode `0600`. It retains responsibility for bounded reading and
removal. The process layer validates the already-owned draft path and inserts it only
into the exact child environment as `AVA_EXTERNAL_EDITOR_FILE`; it neither creates nor
owns the file, and no code mutates AVA's parent environment.

After the editor's private PGID is verified and before its exec gate opens, the process
layer acquires and owns the RAII foreground-terminal lease. The lease opens the
controlling terminal, records AVA's foreground PGID and termios state, temporarily
blocks `SIGTTOU` while calling `tcsetpgrp` for the editor group, and restores AVA's
PGID, termios, and signal mask on every normal, nonzero, signaled, canceled,
launch-failure, and shutdown path. No reservation, process handle, or editor-facing
object exposes a terminal or PID capability.

A foreground transfer or restoration failure never replaces an earlier committed
reason. It marks cleanup incomplete, and the app/editor migration must reject the
edited result rather than accepting draft contents after an unproven terminal restore.
Failure to acquire or transfer the terminal keeps the exec gate closed; absent an
earlier reason, it is a launch failure. Cleanup continues only within the existing
absolute budget.

M1 does not implement a shell-style stopped-job UI. If an editor stops, the monitor
reports `unsupported_suspension`, the lease immediately attempts to restore AVA to the
foreground, and the supervisor sends `SIGCONT` followed by bounded normal escalation
within the existing deadline; it never waits forever on a suspended editor. PTY tests
must prove that the editor observes itself in the foreground, that AVA regains the
terminal and original termios after normal exit, signal, transfer failure, and
suspension, and that restoration failure is incomplete and rejects the draft result.

Browser opening launches the selected opener (`$BROWSER`, `xdg-open`, `gio open`,
`open`, or `wslview`) once as a direct supervised child with `/dev/null` stdio. Its
reservation requires an absolute execution deadline no later than ten seconds after
the reservation instant; forced cleanup may continue only to the fixed two-second
reservation-time horizon described above. Success means the exec handshake succeeded;
monitoring then continues under the application owner without blocking the OAuth flow. M1 removes the
double-fork/`setsid` path. If an opener later daemonizes or calls `setsid`, that escaped
process is outside the stated managed-group guarantee.

### Exact child environment profiles

The parent environment is captured once per AVA invocation into one immutable, bounded
host-environment projection. Capture retains only the union of names sanctioned below,
including the parent-only `BROWSER`, `VISUAL`, and `EDITOR` selectors; it is never
refreshed and the parent environment is never mutated. Role builders receive only this
projection as an ambient input. The bash, plugin, MCP, and Mermaid builders receive no
host projection at all: bash uses its sealed synthetic inputs, MCP uses only selected
explicit configuration, and plugin and Mermaid use fixed/computed values.

Both the host projection and each complete exact environment have at most 256 entries.
An MCP server's explicit `env` subset has at most 64 entries. Names are at most 128
bytes and values at most 16 KiB. The aggregate encoding—name, `=`, value, and trailing
NUL for every entry—is at most 1 MiB. Names contain neither NUL nor `=`; values contain
no NUL; duplicate names fail. `PWD` is reserved to the profile's computed value and
cannot be supplied by a caller or explicit MCP configuration. Each profile fixes a
canonical order for its
fixed and computed entries, and additional inherited `LC_*` entries are ordered
lexicographically by bytewise name.

Every resulting environment is passed with `execve`/descriptor exec; child-side
`setenv` and ambient `execvp` lookup are prohibited. The trusted executable path is
`/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin` unless a row says
otherwise. An explicitly configured MCP `PATH` and any inherited desktop-profile
`PATH` are accepted only when every colon-delimited component is nonempty and absolute.
Omitted MCP `PATH` receives the trusted fixed path. “Inherit” means only a listed name,
once, if present in the captured projection.

| Role / profile ID | Exact child environment |
| --- | --- |
| curl / `ava-curl-v1` | Fixed `PATH` (trusted path), `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, `PWD=/`; inherit only `http_proxy`, `https_proxy`, `ftp_proxy`, `all_proxy`, `no_proxy`, `HTTP_PROXY`, `HTTPS_PROXY`, `FTP_PROXY`, `ALL_PROXY`, `NO_PROXY`, `CURL_CA_BUNDLE`, `SSL_CERT_FILE`, and `SSL_CERT_DIR`. |
| bash / existing `ava-local-bash-prompt-v2` | Preserve the sealed environment from [`EnvironmentFactory`](../../src/ava/command/environment.cpp): `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, `LC_CTYPE=C.UTF-8`, `TZ=UTC`, bounded synthetic `USER` and `LOGNAME`, logical `PWD`, sealed `PATH`, and AVA-created private `HOME`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, and `TMPDIR`. No host projection or ambient additions. |
| plugin / `ava-plugin-minimal-v1` | Fixed trusted `PATH`, `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, and computed `PWD`; no host projection, overrides, inherited variables, ambient proxy/CA, provider/cloud credentials, tokens, or arbitrary `AVA_*`. |
| MCP / `ava-mcp-explicit-v1` | Process-computed `PWD`; the at-most-64 bounded explicit `env` pairs; and trusted `PATH` when those pairs omit `PATH`. Nothing is inherited and no host projection is provided. All launches behave as `clean_environment=true`; explicitly configured secrets remain an intentional user grant, never ambient inheritance. |
| LSP / `ava-lsp-strict-v1` | Preserve [`lsp_environment`](../../src/ava/lsp/lsp_process.cpp): fixed trusted `PATH`, computed `PWD`, and only inherited `HOME`, `USER`, `LOGNAME`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, every other `LC_*`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `TERM`, and `COLORTERM`. |
| Mermaid / `ava-mermaid-v1` | Preserve the exact fixed set: `PATH=/usr/local/bin:/usr/bin:/bin`, `LANG=C.UTF-8`, `LC_ALL=C.UTF-8`, `TERM=dumb`, `NO_COLOR=1`, `PWD=/`, `AVA_MERMAID_PROTOCOL=1`. No host projection. |
| browser opener / `ava-browser-desktop-v1` | Validated bounded inherited `PATH`; inherit only `HOME`, `USER`, `LOGNAME`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, `DISPLAY`, `WAYLAND_DISPLAY`, `XAUTHORITY`, `DBUS_SESSION_BUS_ADDRESS`, `DESKTOP_STARTUP_ID`. `$BROWSER` is resolved by the parent and not forwarded. |
| clipboard helper / `ava-clipboard-desktop-v1` | Fixed trusted `PATH`; inherit only `HOME`, `USER`, `LOGNAME`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `XDG_RUNTIME_DIR`, `DISPLAY`, `WAYLAND_DISPLAY`, `XAUTHORITY`, and `DBUS_SESSION_BUS_ADDRESS`. AVA clipboard test/selection variables are parent-only. |
| external editor / `ava-external-editor-v1` | Validated bounded inherited `PATH`; inherit only `HOME`, `USER`, `LOGNAME`, `SHELL`, `TMPDIR`, `TMP`, `TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `TERM`, `COLORTERM`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_RUNTIME_DIR`, `DISPLAY`, `WAYLAND_DISPLAY`, `XAUTHORITY`, and `DBUS_SESSION_BUS_ADDRESS`; add only child-local `AVA_EXTERNAL_EDITOR_FILE`, whose value is the validated path of the app/editor-owned `0600` draft. `VISUAL`/`EDITOR` are resolved by the parent and not forwarded. The process layer does not create or own the draft. |

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

1. **Foundation and pre-migration hardening:** establish `AVA::process`, closed types,
   explicit app-scoped injection, fake child helpers, and the zero-exception dependency
   rule. Before activating a production family, complete the role-bound environment
   capability, closed launch-surface checks, absolute execution deadline, launch-frame
   classification, and event-driven/adaptive monitor contract in inert focused
   coverage. Land the external-`AnchorOpen` hardening and both anchored factories,
   common descriptor-exec branch, final freshness check, and adoption-cwd requirement
   as a separately reviewed capability commit. That capability commit remains inert
   with respect to production launching: no production caller consumes either
   capability until the later LSP, Bash, and Mermaid waves, and existing path launches
   do not change authority in this wave.
2. **Common noninteractive spawn:** migrate curl and clipboard; prove output/deadline
   behavior, exact-environment bounds, host-projection canaries, and parent-environment
   immutability before removing their local wait/kill helpers. They remain path launches
   because neither role requires descriptor identity.
3. **Protocol children:** migrate plugin, MCP, and LSP without changing their wire
   protocols, permissions, output limits, or one-shot/run-scoped compatibility. Prove
   that plugin and MCP receive no host projection and MCP input comes from the selected
   at-most-64-entry explicit config. Identity-bound LSP recipes become the first common
   `PreopenedExecutableV1` consumers; LSP retains install/root/user/ancestor/workspace/ELF
   provenance and supplies only the neutral expected tuple to `ava_process`.
4. **Secure adoption:** before migrating either caller, remove the temporary cwd
   pathname-open seam and make `AnchoredWorkingDirectoryV1` mandatory at the adoption
   boundary. Then migrate Bash and Mermaid while preserving caller ownership of their
   executable descriptors, descriptor identity, Bash's sealed interpreter/script-FD
   chain, containment, bash-only sentinel, strict environments, and the Mermaid
   startup-stop launch protocol. Remove duplicated process-group code only after parity
   tests pass.
5. **Foreground/application helpers:** migrate external editor and browser. Prove
   app/editor ownership of the `0600` draft, rejection of edited contents after terminal
   transfer/restore failure, and the browser reservation's absolute ten-second maximum;
   run PTY and OAuth opener tests, then statically prove all nine production families
   use the supervisor and no production `fork`/wait/signal owner remains outside the two
   reviewed module backends.

Required tests cover owner-prefix cancellation; reservation exhaustion before fork;
common/adoption role mismatch and non-bash sentinel rejection before fork; exec-gate
and PGID-verification failures; pre-exec versus exec-syscall framing, EOF classification,
and first-reason preservation; past, gated, running, and browser execution deadlines;
exact-PID reaping without stealing an unrelated child; normal/nonzero/signaled exits;
natural leader exit with a surviving same-group descendant; descendant TERM refusal
and escalation; cancellation, timeout, output limit, protocol failure, and concurrent
application shutdown; record eviction; lazy-monitor startup; Linux pidfd-readiness and
wake-descriptor behavior; conservative adaptive-backoff reset and cap; and Mermaid's
consumed startup stop.

Anchored-capability tests additionally cover move-only/no-accessor API shape; relative,
NUL-containing, and non-normalized logical identities; exact comparison of every
`uid`/`gid`/`mode`/`nlink`/`dev`/`inode`/`size`/ctime field; and deterministic
before/opened/after identity races. Error and snapshot canaries prove that no path,
identity tuple, or descriptor value is emitted. Executable mode tests reject nonregular
and nonexecutable targets, hardlinks, and group- or other-writable files while accepting
an otherwise valid owner-writable file. Cwd tests reject a nondirectory and tuple or
logical-spelling mismatch while accepting an identity-matched writable directory.
External-path `AnchorOpen` tests preserve contained/external symlink behavior and
writable-anchor exclusion without a `/proc/self/fd` reopen.

Replacement tests prove that a parent/final-route change before the final pre-fork
freshness check yields no child and does not start the lazy monitor, while a gate-held
child that inherited descriptor A still executes or enters directory A after the
logical path is replaced by B. Executable-only, cwd-only, and capability-absent path
compatibility cases, secure adoption's mandatory anchored cwd, and every
factory/freshness/exec failure prove there is no pathname fallback. A `CLOEXEC`
descriptor-script test expects `ENOENT` as typed `ExecFailed`, while Bash
interpreter-chain tests retain their reviewed script descriptors. FD-hygiene tests
enumerate the exec'd child and failure paths: no anchor, parent-route, cwd, executable,
duplicate stream, gate, or status descriptor leaks across exec, except Bash's
explicitly retained script FDs; every owned capability descriptor closes on all
pre-fork and teardown paths; and Bash/Mermaid executable descriptors remain open under
caller ownership in the parent.

Environment tests cover every allowlist with credential/proxy/CA canaries; the 256/64
entry, 128-byte name, 16-KiB value, and 1-MiB aggregate boundaries and their first
rejected values; duplicate, NUL, `=`, reserved-`PWD`, canonical-order, and absolute-PATH
cases; immutable single-capture behavior; and proof that no parent mutation or forbidden
projection reaches a child. Family tests also cover bash sentinel and containment
parity, LSP/Mermaid executable identity, browser direct parentage, and every
external-editor PTY case above, including short stopped-job detection and rejection of
the draft result when cleanup is incomplete. Leak tests inspect descendants after each
case and disclose the documented `setsid` escape rather than claiming containment.
Linux runs the strong-path suite; another supported POSIX CI lane runs the conservative
contract. Windows factory, descriptor-execution, and lifecycle tests remain deferred
with unsupported Windows design guidance.

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
because that could duplicate a request or tool side effect. Once an anchored capability
is selected, factory, logical-match, freshness, or descriptor-exec failure likewise
never retries through the logical pathname. If a wave regresses its compatibility tests
or benchmark investigation budget, revert that whole family wave to the last passing
authority, keep the inert supervisor foundation, and record the blocker in the program
ledger. M1 is not complete until local ownership code is removed for all nine families;
runtime dual-path flags are not a permanent rollback mechanism.

## Planning gate resolutions

### M1-GATE-001

**Resolved — foreground editor job control.** External editors run in a verified
private group only after the process-owned terminal lease transfers foreground
ownership. The app/editor subsystem owns the `0600` draft; the process layer owns the
lease from verified PGID through restoration and injects only the child environment
path. The lease attempts to restore AVA's foreground group, termios, and signal mask on
every path; transfer/restore failure preserves the first reason, marks cleanup
incomplete, and makes the app reject the edited result. Stopped editors are continued
and terminated with an actionable unsupported-suspension result rather than hanging.
Real PTY tests gate activation.

### M1-GATE-002

**Resolved — environment and credential inheritance.** The role-bound exact-capability
contract and profile table above are implementation prerequisites. One bounded,
immutable host projection contains only sanctioned names; bash, plugin, MCP, and
Mermaid do not receive it. Ambient proxy and CA values are curl-only, while MCP values
come only from selected explicit server configuration. Positive profile/boundary tests
and negative credential, proxy, loader, askpass, agent, and arbitrary-host canaries gate
each migration.

### M1-GATE-003

**Resolved — dependency enforcement.** The foundation wave adds `process` to the
module checker's scanned and recognized module sets, permits only its core dependency,
and adds negative self-tests for process-to-app/agent/tools/protocol includes. The
zero-exception fixture remains unchanged.

These gates resolve design blockers only. They do not assert that the supervisor or any
migration is already implemented.

## Consequences

When implemented and activated, one application owner provides deterministic prefix
cancellation, natural-exit descendant cleanup, bounded diagnostics, and shutdown work
that is parallel rather than per-child cumulative. Process races and environment policy
become testable once, and later persistent plugin work can reuse the same owner/handle
contract.

The cost is a new low-level state machine, one lazy monitor after first use, explicit
capability plumbing through current constructors, stricter plugin/MCP compatibility,
and PTY/platform-specific tests. Process groups remain weaker than containment; escaped
or unkillable descendants and AVA crashes remain honestly outside the guarantee.
