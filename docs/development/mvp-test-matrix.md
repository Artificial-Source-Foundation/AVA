# MVP Test Matrix

Updated: 2026-02-13

## Release Gates

| Gate | Scope | Automated Evidence | Manual Evidence | Status |
|---|---|---|---|---|
| G1 | Auth login + reconnect | `packages/core/src/llm/client.test.ts`, `src/services/auth/oauth.test.ts`, `src/services/auth/oauth-flow.test.ts` | Tauri OAuth smoke (`npm run tauri dev`) | automated_pass |
| G2 | Chat send/stream/queue/steer/cancel | `src/hooks/useChat.integration.test.ts`, `src/components/chat/ChatView.integration.test.tsx` | Chat roundtrip smoke in Tauri | automated_pass |
| G3 | Tool approval + resolution flow | `src/components/chat/ChatView.integration.test.tsx` | Approval prompt resolve/reject in Tauri | automated_pass |
| G4 | Session restore + settings persistence | existing session/settings tests + `npm run test:run` | Restart app and verify persisted state | automated_pass |
| G5 | Plugin lifecycle baseline safety | `packages/core/src/extensions/manager.test.ts`, `src/components/settings/tabs/PluginsTab.smoke.test.tsx` | Plugin tab visible/stable in settings | automated_pass |

## Required Automated Commands

Run all three for MVP confidence:

```bash
npm run lint
npx tsc --noEmit
npm run test:run
```

Fast local gate (single command):

```bash
npm run verify:mvp
```

## Focused Suites

```bash
npx vitest run packages/core/src/llm/client.test.ts src/services/auth/oauth.test.ts src/services/auth/oauth-flow.test.ts
npx vitest run src/hooks/useChat.integration.test.ts src/components/chat/ChatView.integration.test.tsx
npx vitest run packages/core/src/extensions/manager.test.ts src/components/settings/tabs/PluginsTab.smoke.test.tsx
```

## Troubleshooting

- Auth test failure: verify OAuth mocks (`@tauri-apps/api/core`, `@tauri-apps/plugin-opener`) are stubbed before imports.
- Chat integration failure: verify hook/store mocks reset between tests (`vi.clearAllMocks`, `localStorage.clear`).
- Extension test failure: remove temporary extension dirs and re-run the isolated suite.
- Lint/typecheck failures: run affected focused test first, then fix types before rerunning full suite.

## Latest Execution (2026-02-13)

- Focused auth suite: pass (51 tests)
- Focused chat/plugin suites: pass (40 tests)
- Full `npm run test:run`: pass (1798 tests)
- `npm run verify:mvp`: pass
- `npm run tauri dev` smoke: frontend + Rust build startup confirmed after switching linker config to `gcc`
