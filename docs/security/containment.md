# Development Containment

## Overview

AVA's sealed local command runner applies verified Linux development containment
to standard commands that execute mutable project code (build, test, etc.) before
releasing the child process to exec. Containment uses two kernel mechanisms:

1. **Landlock** (filesystem containment): restricts filesystem access to
   explicitly allowed roots.
2. **seccomp** (network containment): blocks all network-related syscalls when
   the sealed command does not explicitly require network access.

## Product Contract

| Command Level | Containment | Permission |
|---------------|-------------|------------|
| Standard (inspection) | Not required | Allow |
| Standard (mutable) + available | Applied before exec | Allow |
| Standard (mutable) + unavailable | Not applied | Critical, one-shot Ask (uncontained warning) |
| Sensitive + available | Applied after approval | Ask |
| Sensitive + unavailable | Not applied | Critical, one-shot Ask (uncontained warning) |
| Critical / Raw / Unverified | Not applied | Ask once |

All commands remain max Once for now. Policy v2 (reusable grants) comes later.

When containment is unavailable, the command is downgraded to Critical risk and
requires explicit one-shot approval. After approval it may run uncontained, but
metadata and audit must state unavailable/uncontained. No reusable grant is
issued. This preserves user authority: unavailable containment must never look
like ordinary Sensitive/Standard containment.

## Root Validation

Root validation is split between synthetic and writable roots:

- **Synthetic roots** (HOME, XDG, TMP): must be exact current-user 0700
  (owner-only, no group/world permissions). These are private, per-run
  directories created by AVA.
- **Writable workspace**: may be current-user-owned 0755 or 0750 (group
  read/execute permitted). Must reject any group/world write bit, symlink,
  special file type, or identity mismatch.

## Authority and Trusted-Home Boundaries

At command sealing:

- The workspace must not equal or be an ancestor of the trusted real home
  directory. Ordinary projects nested under home are allowed.
- The workspace must not overlap with any AVA authority root (config, state,
  sessions, auth directories).
- Synthetic environment roots must be disjoint from workspace, trusted home, and
  all AVA authority roots.

Containment never makes AVA authority roots writable or readable through a
broader workspace rule. Authority roots are explicitly excluded from the
containment allow list.

## What Containment Provides

### Filesystem (Landlock)

Landlock rights are ABI-version-aware:

- **ABI 1**: execute, write_file, read_file, read_dir, remove_dir, remove_file,
  make_char, make_dir, make_reg, make_sock, make_fifo, make_block, make_sym
- **ABI 2**: + `REFER` (rename/link across roots)
- **ABI 3** (minimum required): + `TRUNCATE`
- **ABI 5**: + `IOCTL_DEV` (included in handled mask when supported; writable
  roots may receive it)

- **Handled rights**: every supported Landlock filesystem access right for the
  probed ABI version is handled (denied by default unless explicitly allowed).
- **Writable roots**: the canonical workspace and per-run synthetic HOME/XDG/TMP
  roots receive read/write/create/execute permissions (including IOCTL_DEV when
  ABI >= 5).
- **Read/execute roots**: system roots (`/usr`, `/lib`, `/lib64`, `/bin`,
  `/sbin`, `/etc`), sealed PATH/toolchain entries, and the resolved executable
  directory receive read/execute permissions (READ_FILE, READ_DIR, EXECUTE).
- **Regular-file rules**: file rules use only file-valid rights (READ_FILE and
  EXECUTE as appropriate), never READ_DIR, which Landlock rejects on
  non-directory paths. Redundant recipe-file rules already beneath the writable
  workspace are omitted.
- **Device files**: `/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/random`
  receive read/write permissions. Device files are deliberate: they do not
  receive IOCTL_DEV automatically.
- **Explicitly denied**: the real home directory, AVA config/state/session/auth
  roots, arbitrary `/proc`, external checkout siblings, SSH/cloud/provider
  secrets, and broad `/tmp` are not in the allow list.

### Network (seccomp)

When `network_enabled=false` (the default for standard mutable commands), a BPF
seccomp filter is installed after `PR_SET_NO_NEW_PRIVS` that blocks:

- `socket()` and `socketpair()`: no new sockets can be created, including
  AF_UNIX and abstract sockets.
- `connect()`, `sendto()`, `sendmsg()`, `sendmmsg()`: no data can be sent
  through any descriptor.
- `io_uring_setup()`: prevents io_uring bypass of the socket restriction.

On x86_64, the filter rejects every syscall number with `__X32_SYSCALL_BIT` set
before normal dispatch, preventing x32 ABI bypass. `io_uring_setup` is blocked
unconditionally on both supported architectures (x86_64 and aarch64) using a
stable syscall number (425) verified by static assertion against the kernel
header — no silent header-dependent omission.

Inherited non-stdio file descriptors are closed before the filter is installed
using `close_range(2)` with a bounded fallback, so no pre-existing sockets can
bypass the restriction. Handshake pipe descriptors are never closed.

Seccomp action support is probed in the parent via a non-mutating
`SECCOMP_GET_ACTION_AVAIL` query. If the kernel does not support the required
action, containment is reported as Unavailable.

When `network_enabled=true` (sensitive commands like `curl`, `git fetch`), the
seccomp network filter is omitted. Filesystem containment is retained.
`PR_SET_NO_NEW_PRIVS` is always set (inherited across execve), preventing
privilege escalation through setuid binaries.

## Verified Scope and Limitations

### What is verified

- Landlock filesystem policy **follows descendants** that call `setsid` or
  otherwise change session/group. A contained child and all its descendants
  remain subject to the filesystem access rules.
- seccomp filter policy **follows descendants** in the same way.
- Process-group teardown uses `SIGTERM` → grace period → `SIGKILL` on the
  verified PGID (process group ID), with an AVA-owned sentinel preserving
  the PGID from recycling between leader completion and descendant cleanup.
- Child signal state is reset (default handlers, empty mask) in both the
  leader and sentinel before exec.

### What is NOT claimed

- **No cgroup containment claim.** AVA does not use cgroups to restrict
  resource usage (CPU, memory, PID namespace, etc.) of contained commands.
  Process cleanup is PGID-only; descendants that call `setsid` to escape the
  process group are still subject to Landlock/seccomp policy but cannot be
  signaled via PGID.
- **No namespace isolation claim.** Containment does not use mount namespaces,
  PID namespaces, user namespaces, or network namespaces. The child sees the
  same mount table and PID space as the parent.
- **External OS sandboxing is required** for full containment of descendants
  that call `setsid` and then attempt to create new process trees that might
  outlive the AVA session. Landlock and seccomp prevent filesystem and network
  escapes, but they cannot prevent resource exhaustion or fork bombs.

## Fail-Closed Semantics

- If the Landlock ABI is below the required version, containment is reported
  as Unavailable and standard mutable commands downgrade to Critical Ask.
- If seccomp is not supported or the required action is unavailable,
  containment is reported as Unavailable.
- If the child fails to install Landlock or seccomp rules before exec, the
  parent terminates the verified group and returns an actionable error.
  User code does not start. Commands are never auto-run if child install fails.
- If any allow root fails validation (not owner-only for synthetic, group/world
  writable for workspace, symlink, or identity mismatch), containment is
  reported as Unavailable.

## Truthful Status

- Pre-permission metadata reports containment as `Available` (planned), never
  `Active`. The planning boolean is `network_filter_planned`, not `installed`.
- Summaries say `planned` before fork; `containment_applied` is set to true in
  `BashResult` and emitted process audit only after the parent verifies the
  child installed containment before exec.
- `BashResult` and `ProcessResult` audit include `containment_applied`,
  `containment_profile_id`, and `containment_network_mode` (denied/allowed)
  only after parent verification.
- RPC serialization includes `containment_profile_id` and
  `containment_network_allowed` when command metadata is already serialized.
- Paths that could expose secrets (real home, AVA authority roots, SSH/cloud
  provider paths) are never included in metadata, audit, RPC, or ACP output.

## Metadata and Audit

Permission metadata, audit events, RPC, and ACP surfaces show:
- Containment profile ID and ABI version
- Availability (Available/Unavailable/NotRequired)
- Filesystem scope (workspace writable, synthetic environment writable,
  read-only root count, device file count) — without leaking actual paths
- Network allowed/denied status and planned/applied state
