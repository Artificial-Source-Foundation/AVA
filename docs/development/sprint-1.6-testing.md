# Sprint 1.6: Testing & Debug

TICKET-01: OAuth Unit Tests (JWT + Storage Helpers)

Priority: P0
Effort: M
Files: src/services/auth/oauth.test.ts, src/components/settings/tabs/ProvidersTab.test.tsx
Description: Add unit tests for `decodeJwtPayload`, `extractAccountId`, `checkStoredOAuth`, `clearProviderCredentials`.
Acceptance Criteria:
- decodeJwtPayload handles valid/invalid JWTs
- extractAccountId returns correct claim variants (root, organizations)

TICKET-02: OAuth Integration Tests (Routing + Storage)

Priority: P0
Effort: L
Files: packages/core/src/llm/providers/openai.test.ts, packages/core/src/llm/client.test.ts, src/stores/settings.test.ts
Description: Test credential routing (Anthropic API key vs OpenAI OAuth) and localStorage key storage via `storeOAuthCredentials()`.
Acceptance Criteria:
- OpenAI OAuth routes to Codex endpoint with account header when present
- Anthropic OAuth mints API key and routes via api-key path

TICKET-03: OAuth Manual Test + Fix Session

Priority: P0
Effort: M
Files: docs/development/sprint-1.6-testing.md, docs/memory-bank/activeContext.md
Description: Run `npm run tauri dev` and verify OAuth flows for Anthropic, OpenAI, Copilot; document any failures and fixes.
Acceptance Criteria:
- Each provider completes browser flow and can chat
- Clear credentials works and reconnects cleanly

TICKET-04: Message Flow Unit Tests (useChat)

Priority: P0
Effort: M
Files: src/hooks/useChat.test.ts
Description: Unit tests for queue, steer, cancel, and session switch behavior.
Acceptance Criteria:
- send during streaming queues and auto-dequeues after completion
- steer cancels current stream and sends new message

TICKET-05: Message Flow Integration Tests (Stream + Watcher)

Priority: P1
Effort: L
Files: src/components/chat/ChatView.test.tsx, src/services/file-watcher.test.ts
Description: Integration tests for send→stream→complete flow and file watcher AI comment → chat message.
Acceptance Criteria:
- Chat stream completes and updates UI state
- AI comment triggers auto-send with correct metadata

TICKET-06: Debug Logging Coverage (useChat + useAgent)

Priority: P1
Effort: S
Files: src/hooks/useChat.ts, src/hooks/useAgent.ts
Description: Add structured logs for send/receive/queue/steer/cancel and agent start/finish/tool events.
Acceptance Criteria:
- Logs include source tags and masked credentials
- No new console.log usage

TICKET-07: Debug Logging Coverage (core-bridge + settings + session)

Priority: P1
Effort: S
Files: src/services/core-bridge.ts, src/stores/settings.ts, src/stores/session.ts
Description: Add logging for init, settings sync, and session CRUD/checkpoints.
Acceptance Criteria:
- Init and errors are logged with source tags
- Settings sync errors are logged without secrets

TICKET-08: Debug Logging Coverage (file-watcher + ChatView)

Priority: P1
Effort: S
Files: src/services/file-watcher.ts, src/components/chat/ChatView.tsx
Description: Add logs for watch start/stop, pattern matches, dedup hits, and tool approval events.
Acceptance Criteria:
- Watcher start/stop and errors logged
- Tool approval resolve logged with tool name only

TICKET-09: PI Coding Agent Feature Parity

Priority: P2
Effort: L
Files: docs/backend/gap-analysis.md, docs/ROADMAP.md, packages/core/src/session/*, src/components/chat/*
Description: Scope and implement PI parity items: mid-session provider switching, session branching tree, minimal tool mode, runtime skill creation.
Acceptance Criteria:
- Parity checklist defined with owners and milestones
- Design notes captured in docs

TICKET-10: Console Devtools Improvements

Priority: P2
Effort: M
Files: src/services/dev-console.ts, src/components/panels/TerminalPanel.tsx
Description: Add structured log viewer with filtering by source tag and severity.
Acceptance Criteria:
- Log viewer can filter by source + level
- Logger output is structured and searchable
