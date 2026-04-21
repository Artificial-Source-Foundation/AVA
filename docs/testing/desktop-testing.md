---
title: "Desktop Testing"
description: "Recommended desktop validation flow for AVA after frontend, Tauri, or shared-runtime refactors."
order: 4
updated: "2026-04-16"
---

# Desktop Testing

Use this page when you want to validate the AVA desktop app after a refactor and turn remaining issues into a clean backlog.

## Fast Automated Pass

Run the narrowest useful checks first:

1. `ionice -c 3 nice -n 15 pnpm lint`
2. `ionice -c 3 nice -n 15 pnpm format:check`
3. `ionice -c 3 nice -n 15 pnpm typecheck`
4. `ionice -c 3 nice -n 15 pnpm test:run`
5. `ionice -c 3 nice -n 15 pnpm test:e2e`
6. `ionice -c 3 nice -n 15 just check`

Notes:

1. `pnpm test:e2e` uses Playwright against the Vite-served frontend with Tauri APIs stubbed. It is useful for UI regressions, but it does not prove native Tauri IPC behavior.
2. `just check` is still required before treating a broad refactor as healthy.

## Run The Real Desktop App

Start the desktop shell with:

1. `pnpm tauri dev`

Use a real provider or a safe local setup, then walk the flows below.

## Manual Regression Checklist

### Shell And Navigation

1. App starts cleanly and the splash screen exits into the main shell.
2. Onboarding can be skipped, completed, and reopened from Settings.
3. Settings opens and closes cleanly, and the main sections render correctly.
4. Sidebar toggle, Sessions, and Explorer controls still work.

### Session Lifecycle

1. Create a new session.
2. Switch between at least two sessions.
3. Send a prompt in one session, switch away, then return and confirm the correct session still owns the history and run state.
4. Retry, regenerate, and edit-and-resend on an existing conversation.

### Provider And Model State

1. Open the model picker and change both provider and model.
2. Submit a prompt and confirm the chosen provider/model pair is the one actually used.
3. Reopen the app or reload the desktop shell and confirm the expected model state restores correctly.

### Real Agent Loop

1. Run a simple prompt that should answer without tools.
2. Run a prompt that should read files.
3. Run a prompt that should request a command or file edit.
4. Confirm streaming output, tool progress, completion, and final message settlement all appear on the correct active run.

### Interactive Requests

1. Trigger an approval request.
2. Trigger a question or plan request if available.
3. Queue more than one interactive request and confirm the visible request clears and promotes correctly.
4. Verify timeout/cancel/resolve behavior does not leave stale docks or stale modal state behind.

### Queue And Follow-Up Behavior

1. Submit a prompt while another run is active.
2. Exercise steer, follow-up, and post-complete flows if they are enabled in your workflow.
3. Clear or cancel queued work and confirm only the intended session/run is affected.

### Settings And Integrations

1. Open Providers, Tools, Permissions, Appearance, and Advanced.
2. If you use MCP locally, verify connected/disabled/error states render honestly.
3. If you use OAuth-backed providers, verify connect/success/failure state updates immediately in Settings.

## Useful Existing Coverage

1. `src/hooks/useAgent.test.ts`
2. `src/hooks/useAgent.queue.test.ts`
3. `src/hooks/rust-agent-events.test.ts`
4. `src/components/chat/ApprovalDock.test.tsx`
5. `src/components/chat/QuickModelPicker.test.tsx`
6. `src/components/settings/settings-modal-content.oauth.test.tsx`
7. `src/App.onboarding.test.tsx`
8. `e2e/app.spec.ts`
9. `e2e/web-mode.spec.ts`
10. `e2e/agent-stress.spec.ts`

## How To Backlog Failures

When you find a problem, write it down with:

1. exact surface: desktop, web, TUI, or shared backend
2. exact flow: submit, retry, session switch, approval resolve, model picker, onboarding, and so on
3. expected result
4. actual result
5. reproduction steps
6. whether a unit test, Playwright test, or shared Rust regression is missing
