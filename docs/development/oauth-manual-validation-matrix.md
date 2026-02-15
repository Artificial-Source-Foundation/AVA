# OAuth Manual Validation Matrix

Updated: 2026-02-13

## Goal

Validate desktop OAuth runtime end-to-end for each supported provider and capture evidence for Sprint 1.6 Ticket 03.

## Test Environment

- App mode: `npm run tauri dev`
- Branch: `master`
- Build gate: `npm run verify:mvp` must pass before/after manual run
- Live validation session: `pty_32e46400` (log watch active)

## Matrix

| Provider | Connect OAuth | Send Message | Disconnect | Reconnect | Send After Reconnect | Status | Evidence |
|---|---|---|---|---|---|---|---|
| OpenAI | [ ] | [ ] | [ ] | [ ] | [ ] | pending | |
| Anthropic | [ ] | [ ] | [ ] | [ ] | [ ] | pending | |
| Copilot | [ ] | [ ] | [ ] | [ ] | [ ] | pending | |

## Per-Provider Checklist

1. Open Settings -> Providers.
2. Trigger OAuth connect and complete browser callback.
3. Confirm provider shows connected state.
4. Send chat prompt in fresh session and verify streamed response.
5. Disconnect credentials.
6. Reconnect via OAuth.
7. Send second prompt and verify response.

## Failure Capture Template

- Provider:
- Step failed:
- UI error:
- Console evidence (`DeveloperTab` filter: level=error, source=provider/chat):
- Repro frequency:
- Suspected root cause:
- Fix PR/commit:

## Exit Criteria (Ticket 03)

- All matrix cells checked for all providers.
- At least one successful post-reconnect send per provider.
- No unresolved P0 OAuth blocker remains.
