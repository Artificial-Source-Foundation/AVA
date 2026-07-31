# Session Architecture

This orients contributors to the core runtime abstractions behind AVA's session
subsystem: what a session *is*, what holds it, and how the runtime reads and
writes it safely. It is the conceptual prerequisite for the more specialized
engineering docs:

- [`session-format.md`](../session-format.md) — the public on-disk JSONL format
  (envelope fields, entry types, validation).
- [`session-versioning.md`](session-versioning.md) — entry/payload versioning
  and recovery policy.
- [`session-run-controller.md`](session-run-controller.md) — the run controller
  and append-routing contract, including its "Runtime read authority" section.

The implementation lives under `src/ava/session/` (storage, lease, authorities)
and `src/ava/app/runtime/` (the live `Session` wrapper). The terms are collected
in the [Glossary](#glossary) at the end.

## Sessions: durable identity vs. live process

A **session** is one conversation's append-only history, stored as a JSONL file
with one JSON object per line. The on-disk path is:

```
<session-root>/<workspace-key>/<session_id>.jsonl
```

where `<workspace-key>` is a stable hash of the absolute, normalized workspace
path, so sessions are scoped per project directory. Each line is one
`SessionEntry` (`src/ava/session/session_store.h`):

```cpp
struct SessionEntry {
  std::string id;          // this entry's id
  std::string parent_id;   // links entries into a tree (empty == no parent)
  EntryType type;          // user_message, assistant_message, tool_call, ...
  std::string timestamp;   // UTC "YYYY-MM-DDTHH:MM:SSZ"
  std::string data_json;   // type-specific payload as a JSON object string
  long long version;       // current writer version is 4
};
```

`EntryType` is the vocabulary of events that can be recorded: `user_message`,
`assistant_message`, `tool_call`, `tool_result`, `permission_decision`,
`mode_change`, `model_change`, `reasoning_block`, `compaction`, and so on. See
[`session-format.md`](../session-format.md) for the full entry-type table and
payload shapes.

Two distinct C++ types both get called "session", and the split matters:

- **`ava::session::SessionStore`** is the *durable on-disk identity* of one
  session file. It knows the `session_id`, computes `session_path()`, and owns
  the read/write logic. For a persistent session it holds **no entries in
  memory** — the entries live in the JSONL. Its data members are just
  `options_` (`root_dir`, `workspace_dir`, `session_id`), an optional
  `ephemeral_state_` (see [Sessionless and ephemeral
  mode](#sessionless-and-ephemeral-mode)), and an observation attachment for
  tracing.

- **`ava::app::runtime::Session`** (`src/ava/app/runtime/Session.h`) is the
  *live, in-process* wrapper. It **contains a `store`** member plus everything
  that cannot round-trip through disk: the active model and reasoning selection,
  the resolved prompt state, project trust, the lease, the run controller,
  pre-opened anchor descriptors, and MCP config. This is why test and runtime
  code writes `session->store` — `Session` exposes its inner `SessionStore`.

## Reading history: `SessionStore::load()` and the lease-bound overloads

`SessionStore` exposes two families of read entry points with very different
trust models (`src/ava/session/session_store.h`):

- **`load()` / `load_bounded(limits)` (no lease)** — the *unprivileged path
  read*. It opens `session_path()` with a plain `ifstream`, reads line by line,
  and parses each into a `SessionEntry`. It trusts **whatever the path currently
  names**. This is used for diagnostics, non-current session listing/tree
  metadata, and legacy inactive compatibility adapters.

- **`load(lease)` / `load_bounded(lease, limits)`** — the *authoritative,
  tamper-detecting* reads, pinned to the leased inode (see below). These feed
  runtime history consumers: the agent loop / message builder, provider tool-call
  reconstruction, model compatibility validation, compaction snapshots,
  current-session commands and permissions, ACP, and RPC serialization.

The distinction is the heart of AVA's session integrity model: a path is just a
*name*, and the runtime must not trust a name alone for the history it sends to
a model.

## The session lease

A **`SessionLease`** (`src/ava/session/session_store.h`) is an exclusive
advisory file lock held through an open file descriptor pinned to **one specific
inode**:

```cpp
class SessionLease {
  int fd_ = -1;                          // open descriptor to the file
  std::filesystem::path canonical_path_; // the path it was acquired on
  bool created_ = false;
};
```

It is acquired with `create_and_acquire` / `acquire` (the final path component
opened with `O_NOFOLLOW`, the descriptor marked `CLOEXEC`, an advisory lock
taken) and the lock is released automatically on destruction (RAII). A
persistent runtime owner holds this lease for the complete session lifetime, and
it is **cross-process**.

Why a lease exists: a pathname is a label, an inode is the actual file. Another
process — or a branch / recovery operation — can **replace** the file at a path
(rename the old one away, drop a new file in its place) without the path string
changing. The lease lets the runtime **prove it still holds the exact file it
opened**: the descriptor references a specific inode, and
`same_file_identity` (comparing `st_dev`/`st_ino`) tests whether the path still
names that same inode. `duplicate()` uses `F_DUPFD_CLOEXEC` so a copy of the
lease preserves exact inode identity without ever reacquiring by pathname.

## Lease-bound reads and authorities

A **lease-bound read** is pinned to the leased inode rather than trusting the
path, and is hardened against time-of-check-to-time-of-use (TOCTOU) replacement.
The flow lives in `SessionStore::visit_entries_leased`
(`src/ava/session/session_store_read.cpp`):

1. **Validate identity "before snapshot"** — `fstat` the lease descriptor and
   `fstatat` the pathname. Require them to be the *same inode*
   (`same_file_identity`) with `st_nlink == 1` each, proving the path still
   names the leased file and that file has exactly one link.
2. **Read the snapshot through the lease descriptor** — the bytes come from the
   exact inode held, not from reopening the path.
3. **Validate identity "after snapshot"** — re-check. If the inode changed
   *during* the read, the read is rejected ("lease-bound session read target was
   replaced after snapshot").

Two **authorities** are built on the lease (both `duplicate()` it, preserving
inode identity):

- **`SessionReadAuthority`** — the copyable history-read capability. A persistent
  authority owns a copied `SessionStore` and a duplicated matching lease.
  `create_persistent` re-validates that the leased inode, the path-opened inode,
  and the published inode all agree and each have `nlink == 1`, *then* duplicates
  the descriptor. If a path has already been replaced, authority *creation*
  fails ("persistent read authority does not identify one regular leased inode").
- **`SessionAppendTarget`** — the sole append authority. All runtime writes flow
  through it; it revalidates the published inode before accepting it and latches
  a persistence error until explicit recovery.

The combined property is **fail-closed on pathname replacement**: a live swap of
the session file is detected and refused rather than silently feeding replaced
content into provider, compaction, or RPC context. The intentional exception is
the unprivileged path read above, used only where non-authoritative observation
is acceptable.

| Read style | Trusts the path? | Tamper-detecting? | Used for |
| --- | --- | --- | --- |
| `store.load()` / `load_bounded()` (no lease) | Yes | No | diagnostics, listing, legacy inactive adapters |
| `load(lease)` / via `SessionReadAuthority` | No — pinned to inode | Yes | authoritative runtime history (model context) |

## Sessionless and ephemeral mode

`--no-session` selects **sessionless** mode. In the runtime this maps directly
to an **ephemeral** store: `SessionStore::create_ephemeral(workspace_dir)`
(`src/ava/app/runtime/Session.cpp`), and `store.is_ephemeral()` is the runtime
test. An ephemeral store keeps its entries in `ephemeral_state_` (shared
in-memory state) instead of a JSONL file, writes no resumable file, and holds no
lease. Consequently an ephemeral read authority owns only the shared in-memory
state (no lease descriptor), and an ephemeral append target mutates that same
in-memory state. In-process commands can still use runtime entries, but nothing
is resumable after the process exits. Sessionless mode is mutually exclusive
with session resume/fork options.

## Glossary

- **Session** — one conversation's append-only history as a JSONL file, scoped
  to a workspace directory by `<workspace-key>`. Each line is one `SessionEntry`.
- **`SessionStore`** — the durable on-disk identity of one session file: its
  `session_id`, `session_path()`, and read/write logic. Holds no entries in
  memory for persistent sessions.
- **`runtime::Session`** — the live in-process wrapper that *contains* a
  `SessionStore` plus non-durable state (model, prompt, trust, lease, run
  controller, anchors, MCP).
- **Session lease (`SessionLease`)** — an exclusive advisory lock held through an
  open descriptor pinned to one specific inode; proves the runtime still owns the
  exact file it opened across pathname replacements. Cross-process, RAII.
- **Lease-bound read** — an authoritative read pinned to the leased inode with
  before/after-snapshot identity validation, so a mid-read file replacement is
  detected and rejected.
- **`SessionReadAuthority`** — the copyable, lease-backed history-read capability
  handed to runtime history consumers; validates inode/nlink at binding and on
  every read.
- **`SessionAppendTarget`** — the sole append authority; all runtime writes flow
  through it, with failure latching until explicit recovery.
- **Pathname read** — an unprivileged `SessionStore::load()` that reads whatever
  the path currently names, used only for non-authoritative observation.
- **Sessionless / ephemeral** — `--no-session` mode backed by an in-memory store
  (`create_ephemeral`); no JSONL file, no lease, not resumable. Tested via
  `store.is_ephemeral()`.
- **Ephemeral state** — the shared in-memory entry store (`ephemeral_state_`)
  used in place of a JSONL file by ephemeral stores, authorities, and append
  targets.
- **Fail-closed on replacement** — the invariant that a live pathname swap is
  refused (by lease-bound reads and authority creation) rather than feeding
  replaced content into runtime context.
