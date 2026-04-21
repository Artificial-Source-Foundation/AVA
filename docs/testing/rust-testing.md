---
title: "Rust Testing"
description: "Rust workspace verification commands and when to use them."
order: 2
updated: "2026-04-18"
---

# Rust Testing

## Main Commands

Use `just` for the normal Rust workflow:

1. `just check`
2. `just test`
3. `just test-all`
4. `just lint`
5. `just fmt`
6. `just ci`

`just check` is the pragmatic local Rust gate. `just ci` is a broader local verification pass, but CI is still the authoritative full-suite gate.

## When To Use What

1. Use narrow crate-level `cargo test -p <crate>` while iterating.
2. Use `just check` before considering a Rust change done.
3. Use broader workspace checks before merging or releasing larger changes.

## Why This Matters

AVA is a Rust-first system with cross-crate runtime behavior. Narrow tests help iteration speed; broad checks catch integration drift.
