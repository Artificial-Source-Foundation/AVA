# Development Containment

## Overview

AVA's sealed local command runner applies verified Linux development containment
to standard commands that execute mutable project code (build, test, etc.) before
releasing the child process to exec. Containment uses two kernel mechanisms:

1. **Landlock** (filesystem containment): restricts filesystem access to
   explicitly allowed roots.
2. **seccomp** (network containment): blocks outbound IP network paths when the
   sealed command does not explicitly require network access.

## Product Contract

| Command Level | Containment | Permission |
|---------------|-------------|------------|
| Standard (inspection) | Not required | Allow |
| Standard (mutable) + available | Applied before exec | Allow |
| Standard (mutable) + unavailable | Not applied | Ask (downgrade) |
| Sensitive | Applied after approval | Ask |
| Critical / Raw / Unverified | Not applied | Ask once |

All commands remain max Once for now. Policy v2 (reusable grants) comes later.

## What Containment Provides

### Filesystem (Landlock)

- **Handled rights**: every supported Landlock filesystem access right,
  including `TRUNCATE` and `REFER`, is handled (denied by default unless
  explicitly allowed). This requires Landlock ABI >= 3.
- **Writable roots**: the canonical workspace and per-run synthetic
  HOME/XDG/TMP root receive read/write/create/execute permissions.
- **Read/execute roots**: system roots (`/usr`, `/lib`, `/lib64`, `/bin`,
  `/sbin`, `/etc`), sealed PATH/toolchain entries, and the resolved
  executable directory receive read/execute permissions.
- **Device files**: `/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/random`
  receive read/write permissions.
- **Explicitly denied**: the real home directory, AVA config/state/session/auth
  roots, arbitrary `/proc`, external checkout siblings, SSH/cloud/provider
  secrets, and broad `/tmp` are not in the allow list.

### Network (seccomp)

- When `network_enabled=false` (the default for standard mutable commands), a
  BPF seccomp filter is installed after `PR_SET_NO_NEW_PRIVS` that blocks
  `socket(AF_INET)` and `socket(AF_INET6)` with `EPERM`, and blocks
  `io_uring_setup` to prevent io_uring bypass.
- Inherited non-stdio file descriptors are closed before the filter is
  installed, so no pre-existing IP sockets can bypass the restriction.
- When `network_enabled=true` (sensitive commands like `curl`, `git fetch`),
  the seccomp network filter is omitted. Filesystem containment is retained.
- `PR_SET_NO_NEW_PRIVS` is always set (inherited across execve), preventing
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
  as Unavailable and standard mutable commands downgrade to Ask.
- If seccomp is not supported on the architecture, containment is reported as
  Unavailable.
- If the child fails to install Landlock or seccomp rules before exec, the
  parent terminates the verified group and returns an actionable error.
  User code does not start.
- If any allow root fails validation (not owner-only, is a symlink, etc.),
  containment is reported as Unavailable.

## Metadata and Audit

Permission metadata, audit events, RPC, and ACP surfaces show:
- Containment profile ID and ABI version
- Availability (Available/Unavailable/NotRequired)
- Filesystem scope (workspace writable, synthetic environment writable,
  read-only root count, device file count) — without leaking actual paths
- Network allowed/denied status

Paths that could expose secrets (real home, AVA authority roots, SSH/cloud
provider paths) are never included in metadata, audit, RPC, or ACP output.
