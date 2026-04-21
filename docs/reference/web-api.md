---
title: "Web API Surface"
description: "Current AVA web backend HTTP and WebSocket routes, with debug/stub caveats."
order: 7
updated: "2026-04-19"
---

# Web API Surface

This page documents the currently implemented web backend surface for `ava serve`.

Availability: this surface exists only in builds compiled with the `web` feature. Default AVA installs do not include the `ava serve` runtime.

This is implementation reference for local web development and integration work, not a normal end-user quick-start page.

## Stability Note

These routes are the current frontend/backend integration surface for `ava serve`.

They should be treated as **implementation-coupled** unless explicitly promoted as a stable public API.

## Base Endpoints

1. HTTP API prefix: `/api/...`
2. WebSocket stream: `/ws`
3. Health check: `/api/health`

## Local Security Defaults

`ava serve` is a developer web surface, so the runtime now defaults to the narrowest practical local posture:

1. Bind host defaults to `127.0.0.1`.
2. Browser CORS/origin policy allows loopback browser origins only (`localhost`, `127.0.0.1`, `[::1]`).
3. If `--token` is not supplied, AVA generates a control token at startup; the raw value is shown only on the live terminal, while normal logs keep it redacted.
4. Sensitive HTTP routes require `Authorization: Bearer <token>` (or `x-ava-token: <token>`).
5. WebSocket clients must connect with `/ws?token=<token>`; `/ws?access_token=<token>` is accepted as a compatibility alias.
6. `--insecure-open-cors` is an explicit opt-in escape hatch if you intentionally need non-loopback browser origins during local development.

Health plus a small set of non-session discovery routes remain comparatively permissive for local DX; session history/detail reads, plan persistence reads (`/api/plans` + `/api/plans/{filename}`), live control-plane state reads, high-risk plugin/CLI discovery + route-proxy execution surfaces, privileged mutations, and the WebSocket stream are token-protected.

## Route Groups (Current)

### Agent control

1. `POST /api/agent/submit`
2. `POST /api/agent/cancel`
3. `GET /api/agent/status`
4. `POST /api/agent/retry`
5. `POST /api/agent/edit-resend`
6. `POST /api/agent/regenerate`
7. `POST /api/agent/undo`
8. `POST /api/agent/resolve-approval`
9. `POST /api/agent/resolve-question`
10. `POST /api/agent/resolve-plan`
11. `POST /api/agent/steer`
12. `POST /api/agent/follow-up`
13. `POST /api/agent/post-complete`
14. `GET /api/agent/queue`
15. `POST /api/agent/queue/clear`

Token-protected in this group: all routes above, including read-only `status` and `queue`, because they expose live control-plane state and pending interactive requests.

`POST /api/agent/submit` accepts the same optional per-run context fields the shared frontend contract already uses for desktop submit flows: `provider`, `model`, `thinkingLevel`/`thinking_level`, `sessionId`/`session_id`, `runId`/`run_id`, `autoCompact`/`auto_compact`, `compactionThreshold`/`compaction_threshold`, `compactionProvider`/`compaction_provider`, `compactionModel`/`compaction_model`, plus optional `images`.

Replay routes (`POST /api/agent/retry`, `POST /api/agent/edit-resend`, `POST /api/agent/regenerate`) do not accept fresh per-run context overrides today; instead they reuse the session's persisted `runContext` metadata so provider/model/thinking/compaction behavior matches the run being replayed.

### Session and message operations

1. `GET /api/sessions`
2. `POST /api/sessions/create`
3. `POST /api/sessions/search`
4. `GET /api/sessions/{id}`
5. `DELETE /api/sessions/{id}`
6. `POST /api/sessions/{id}/rename`
7. `POST /api/sessions/{id}/archive`
8. `POST /api/sessions/{id}/unarchive`
9. `POST /api/sessions/{id}/duplicate`
10. `GET /api/sessions/{id}/messages`
11. `POST /api/sessions/{id}/message`
12. `PATCH /api/sessions/{id}/messages/{msg_id}`

Token-protected in this group: all routes above, including list/search/detail/message-read routes, because they expose session history, previews, and transcript contents.

### Session sub-resource routes (currently stub-oriented)

1. `GET /api/sessions/{id}/agents`
2. `GET /api/sessions/{id}/files`
3. `GET /api/sessions/{id}/terminal`
4. `GET /api/sessions/{id}/memory`
5. `GET /api/sessions/{id}/checkpoints`

These are present in route wiring and labeled as "stub" in module docs.

### Body-based session compatibility routes

1. `POST /api/sessions/delete`
2. `POST /api/sessions/rename`
3. `POST /api/sessions/load`

`/api/sessions/load` inherits the same token requirement as `GET /api/sessions/{id}` because it returns the same session detail payload.

### MCP / plugins / model-config surfaces

1. `GET /api/mcp`
2. `POST /api/mcp/reload`
3. `POST /api/mcp/servers/{name}/enable`
4. `POST /api/mcp/servers/{name}/disable`
5. `GET /api/plugins`
6. `GET /api/plugins/mounts`
7. `POST /api/plugins/{plugin}/commands/{command}`
8. `GET|POST /api/plugins/{plugin}/routes/{*route_path}`
9. `GET /api/models`
10. `GET /api/models/current`
11. `POST /api/models/switch`
12. `GET /api/providers`
13. `GET /api/cli-agents`
14. `GET /api/config`
15. `POST /api/tools/agent`
16. `GET|POST /api/permissions`
17. `POST /api/permissions/toggle`
18. `GET /api/plans`
19. `GET /api/plans/{filename}`
20. `POST /api/context/compact`
21. `POST /api/log`

Token-protected here: MCP reload/enable/disable, plugin mount discovery, all plugin route proxying (`GET|POST`), plugin command POSTs, CLI-agent discovery, model switching, config reads, permission reads/writes, plan listing/loading routes, and context compaction. Public/local-read routes now narrow down to model/provider listing, plugin listing, and the frontend log sink.

Example wildcard plugin route: `GET /api/plugins/example-plugin/routes/v1/status`

## Debug-Only Routes

When compiled with `debug_assertions`, two additional routes are exposed:

1. `POST /api/debug/inject-approval`
2. `POST /api/debug/finish-run`

These should be considered development/test helpers only.

## Related

1. [Commands](commands.md)
2. [Filesystem layout](filesystem-layout.md)
3. [Configuration](configuration.md)
