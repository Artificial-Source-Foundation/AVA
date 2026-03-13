# Epic: Sprint 1.6 Testing and Hardening

Status update date: 2026-02-14

## Objective

Ship a high-confidence MVP baseline by hardening auth flow reliability, chat stream/message flow behavior, plugin baseline safety checks, and verification automation.

## Current Status

- Status: in progress
- Automated baseline: pass (`npm run test:run` -> 70 files, 1801 tests)
- Verification pipeline: pass (`npm run verify:mvp`)
- Remaining gate: manual desktop OAuth runtime matrix

## Done

- OAuth unit/integration automation landed across:
  - `src/services/auth/oauth-flow.test.ts`
  - `src/services/auth/oauth.test.ts`
  - `packages/core/src/llm/client.test.ts`
- Chat reliability automation landed across:
  - `src/hooks/useChat.integration.test.ts`
  - `src/components/chat/ChatView.integration.test.tsx`
- Plugin baseline smoke/regression checks landed:
  - `src/components/settings/tabs/PluginsTab.smoke.test.tsx`
  - `packages/core/src/extensions/manager.test.ts`
- MVP verification command pipeline added and passing:
  - `scripts/testing/verify-mvp.sh`
  - `npm run verify:mvp`
- Chat streaming micro-jitter stabilization completed for stream start/end transitions.

## In Progress

- Structured logging hardening pass across chat/agent/core-bridge/settings/session/file-watcher surfaces
- Manual OAuth matrix execution in desktop runtime for OpenAI, Anthropic, and Copilot

## Next

1. Complete provider-by-provider manual OAuth runtime matrix.
2. Close Sprint 1.6 with an evidence refresh in matrix/readiness docs.
3. Publish Sprint 1.6 closeout in `docs/development/sprints/2026-S1.6-testing-hardening-closeout.md`.

## Dependencies

- Local desktop environment for OAuth callback flow validation
- Provider OAuth credentials and callback routing availability

## Exit Criteria

- Manual OAuth matrix complete for OpenAI, Anthropic, Copilot.
- No blocker in `npm run verify:mvp`.
- Sprint evidence docs aligned and dated with latest run.

## Evidence Sources

- `docs/development/sprints/sprint-1.6-testing.md`
- `docs/development/sprints/mvp-test-matrix.md`
- `docs/development/status/mvp-readiness-report-2026-02-13.md`
