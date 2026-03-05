# Sprint 9: Testing & Platform QA

**Epic:** C — Ship
**Duration:** 1 week
**Goal:** Everything works on every platform, E2E tests pass

---

## Story 9.1: E2E Tests with Real Backend

**Reference:** Agent 3 built the test harness at `tests/e2e/`

**What to do:**
1. Switch test harness from mock mode to real Rust backend
2. Run test scenarios:

| Test | What it verifies |
|---|---|
| Edit a file via agent | Edit cascade + validation + Rust fuzzy match |
| Search codebase | Grep via Rust compute_grep |
| Multi-file task with delegation | Commander → Lead → Worker pipeline |
| Plugin load/unload | Plugin lifecycle |
| Memory persist across sessions | Rust ava-memory SQLite |
| Permission approval flow | Rust ava-permissions + dynamic rules |
| Sandbox execution | Rust ava-sandbox bwrap/seatbelt |
| Git checkpoint + rollback | Ghost commits |
| Context compaction | Multi-strategy cascade |
| MCP server connection | MCP client + tool registration |

**Acceptance criteria:**
- [ ] All 10 E2E scenarios pass
- [ ] Tests run in CI (GitHub Actions)
- [ ] Test report generated

---

## Story 9.2: Platform Testing

**Platforms to test:**

| Platform | Specific versions | Key concerns |
|---|---|---|
| Linux | Ubuntu 22.04, 24.04, Fedora 40, Arch | bwrap sandbox, PTY, file watchers |
| macOS | Intel (Monterey+), Apple Silicon (Ventura+) | sandbox-exec, codesign, notarization |
| Windows | 10, 11, WSL2 | PTY (ConPTY), path separators, no sandbox |

**Test checklist per platform:**
- [ ] App launches without errors
- [ ] Agent runs and completes a task
- [ ] Settings persist
- [ ] PTY works (interactive commands)
- [ ] File operations work (read/write/edit)
- [ ] Sandbox works (Linux/macOS only)
- [ ] Auto-updater checks for updates

**Acceptance criteria:**
- [ ] Works on 3+ Linux distros
- [ ] Works on macOS Intel + Apple Silicon
- [ ] Works on Windows 10 + 11
- [ ] Zero console errors on clean startup

---

## Story 9.3: Regression Testing

**What to verify:**
1. All existing tests pass: `npm run test:run` (5,350+ tests)
2. Rust tests pass: `cargo test --workspace`
3. No console errors/warnings on startup
4. Memory usage < 100MB idle
5. Startup time < 1s

**Fix any regressions** from Sprints 1-8.

**Known pre-existing failures to resolve:**
- `ChatView.integration.test.tsx` — solid-motionone .jsx extension issue
- `extension-loader.test.ts` — flaky in full suite

**Acceptance criteria:**
- [ ] 5,350+ TS tests pass
- [ ] All Rust crate tests pass
- [ ] Zero console errors on startup
- [ ] Memory < 100MB idle
- [ ] Startup < 1s
