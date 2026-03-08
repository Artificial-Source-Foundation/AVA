# Sprint 1.6: Testing & Debug

Status update date: 2026-02-13

## Ticket Board

| Ticket | Title | Priority | Status | Evidence | Notes |
|---|---|---|---|---|---|
| TICKET-01 | OAuth Unit Tests (JWT + Storage Helpers) | P0 | done | `packages/core/src/llm/client.test.ts`, `src/services/auth/oauth.test.ts`, `src/services/auth/oauth-flow.test.ts` | Added reconnect/clear-provider coverage and additional `getAuth` edge tests. |
| TICKET-02 | OAuth Integration Tests (Routing + Storage) | P0 | done | `src/services/auth/oauth.test.ts`, `src/services/auth/oauth-flow.test.ts`, `packages/core/src/llm/client.test.ts` | OpenAI/Copilot/Anthropic auth flow behavior and storage routing covered in automated tests. |
| TICKET-03 | OAuth Manual Test + Fix Session | P0 | in_progress | `docs/development/sprints/oauth-manual-validation-matrix.md` | Matrix and evidence template added; provider-by-provider manual run still in progress. |
| TICKET-04 | Message Flow Unit Tests (useChat) | P0 | done | `src/hooks/useChat.integration.test.ts` | Added queue/steer/cancel behavior assertions with mocked streaming. |
| TICKET-05 | Message Flow Integration Tests (Stream + Watcher) | P1 | done | `src/components/chat/ChatView.integration.test.tsx` | Added watcher->message forwarding test including question prefix and file context metadata. |
| TICKET-06 | Debug Logging Coverage (useChat + useAgent) | P1 | in_progress | `src/hooks/useChat.ts`, `src/hooks/useAgent.ts` | `useChat` logging is structured; remaining work is final `useAgent` audit pass. |
| TICKET-07 | Debug Logging Coverage (core-bridge + settings + session) | P1 | in_progress | `src/services/core-bridge.ts`, `src/stores/settings.ts`, `src/stores/session.ts`, `src/services/settings-fs.ts`, `src/stores/project.ts` | Added structured logger usage in settings/project paths and removed ad-hoc console warnings. |
| TICKET-08 | Debug Logging Coverage (file-watcher + ChatView) | P1 | in_progress | `src/services/file-watcher.ts`, `src/components/chat/ChatView.tsx` | Added debug-level read-skip logging in watcher; chat approval/comment logs remain structured. |
| TICKET-09 | PI Coding Agent Feature Parity | P2 | done | `docs/development/status/pi-parity-scope-2026-02-13.md` | Prioritized parity scope with estimates and milestone mapping documented. |
| TICKET-10 | Console Devtools Improvements | P2 | done | `src/components/settings/tabs/DeveloperTab.tsx`, `src/services/dev-console.ts` | Console viewer now supports level + source + text filtering and jump-to-latest behavior. |

Status legend: `todo`, `in_progress`, `done`, `blocked`.

## Verification Evidence (2026-02-13)

- `npx vitest run packages/core/src/llm/client.test.ts src/services/auth/oauth.test.ts src/services/auth/oauth-flow.test.ts` -> pass (51 tests)
- `npx vitest run src/hooks/useChat.integration.test.ts src/components/chat/ChatView.integration.test.tsx src/components/settings/tabs/PluginsTab.smoke.test.tsx packages/core/src/extensions/manager.test.ts` -> pass (40 tests)
- `npm run test:run` -> pass (70 files, 1801 tests)
- `npm run verify:mvp` -> pass

## Acceptance Criteria (Unchanged)

### TICKET-01
- `decodeJwtPayload` handles valid/invalid JWTs
- `extractAccountId` returns correct claim variants (root, organizations)

### TICKET-02
- OpenAI OAuth routes to Codex endpoint with account header when present
- Anthropic OAuth mints API key and routes via api-key path

### TICKET-03
- Each provider completes browser flow and can chat
- Clear credentials works and reconnects cleanly

### TICKET-04
- Send during streaming queues and auto-dequeues after completion
- Steer cancels current stream and sends new message

### TICKET-05
- Chat stream completes and updates UI state
- AI comment triggers auto-send with correct metadata

### TICKET-06
- Logs include source tags and masked credentials
- No new `console.log` usage

### TICKET-07
- Init and errors are logged with source tags
- Settings sync errors are logged without secrets

### TICKET-08
- Watcher start/stop and errors logged
- Tool approval resolve logged with tool name only

### TICKET-09
- Parity checklist defined with owners and milestones
- Design notes captured in docs

### TICKET-10
- Log viewer can filter by source + level
- Logger output is structured and searchable
