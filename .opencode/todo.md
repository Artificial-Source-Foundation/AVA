# Mission: MVP readiness for auth + autonomous testing

## M1: Define MVP gates and tracking | status: completed
### T1.1: Create MVP quality matrix | agent:Commander
- [x] S1.1.1: Add `docs/development/mvp-test-matrix.md` with release gates | size:S
- [x] S1.1.2: Wire gate evidence links as tests land | size:S

## M2: Auth reliability hardening | status: completed
### T2.1: Core auth test coverage | agent:Commander
- [x] S2.1.1: Expand `packages/core/src/llm/client.test.ts` edge cases | size:S
- [x] S2.1.2: Add frontend auth reconnect coverage in `src/services/auth/oauth-flow.test.ts` | size:M

## M3: Chat + plugin safety automation | status: completed
### T3.1: Chat integration reliability tests | agent:Commander
- [x] S3.1.1: Add `src/hooks/useChat.integration.test.ts` | size:M
- [x] S3.1.2: Add `src/components/chat/ChatView.integration.test.tsx` | size:M

### T3.2: Plugin smoke coverage | agent:Commander
- [x] S3.2.1: Add lifecycle regression case in `packages/core/src/extensions/manager.test.ts` | size:S
- [x] S3.2.2: Add `src/components/settings/tabs/PluginsTab.smoke.test.tsx` | size:S

## M4: Autonomous verification + reporting | status: completed
### T4.1: Verification command pipeline | agent:Commander
- [x] S4.1.1: Add `scripts/verify-mvp.sh` and `verify:mvp` script | size:S
- [x] S4.1.2: Run focused + full verification commands | size:M

### T4.2: Sprint/docs synchronization | agent:Commander
- [x] S4.2.1: Update sprint 1.6 ticket statuses/evidence | size:S
- [x] S4.2.2: Update roadmap/backlog + readiness report | size:M
- [x] S4.2.3: Run manual Tauri OAuth sanity pass and clear repo lint/type baseline blockers | size:M
