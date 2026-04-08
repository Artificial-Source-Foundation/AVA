# HQ Plugin Example

This is the first real HQ plugin artifact for the AVA 3.3 plugin-boundary migration.

It re-registers HQ-owned commands, routes, events, and mount metadata through the existing plugin host seam instead of restoring HQ into core startup.

## What It Exposes

1. Commands: `hq.status`, `hq.roles.list`
2. Routes: `GET /status`, `GET /roles`
3. Event: `hq.status.requested`
4. Mount metadata:
5. `sidebar.panel` -> `hq.dashboard`
6. `settings.section` -> `hq.settings`

## Local Development

1. Optionally prebuild the plugin binary for faster startup:
   `cargo build -p ava-hq --bin ava-hq-plugin`
2. Link this directory into AVA as a local plugin.
3. AVA will discover the `plugin.toml` and start the wrapper script.
4. The wrapper prefers a prebuilt `target/debug/ava-hq-plugin` when it exists and otherwise falls back to `cargo run --quiet -p ava-hq --bin ava-hq-plugin` for development.

This is intentionally a development-first slice. It proves that HQ can come back only through plugin-owned registration, while the deeper HQ runtime/UI/storage reintroduction stays as follow-up work.
