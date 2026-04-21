---
title: "Frontend Testing"
description: "Frontend and desktop verification commands for AVA's JS/TS and Tauri surfaces."
order: 3
updated: "2026-04-10"
---

# Frontend Testing

## Main Commands

Use `pnpm` for frontend and desktop verification:

1. `pnpm lint`
2. `pnpm format:check`
3. `pnpm typecheck`
4. `pnpm test`
5. `pnpm test:e2e`
6. `pnpm tauri dev`

## When To Use What

1. Use `lint` and `typecheck` for quick frontend confidence.
2. Use `test` when logic or UI behavior changes.
3. Use `test:e2e` for important product-surface paths.
4. Use `tauri dev` when desktop integration or IPC changes.
