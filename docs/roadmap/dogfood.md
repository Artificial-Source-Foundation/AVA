# AVA dogfood qualification

Status: active qualification on `develop` (2026-07-23).

AVA is ready for regular repository dogfooding. Linux release closure is implemented for qualified x86_64/x64 source-built artifacts; non-x86 architectures remain unqualified pending provenance or replacement of the non-Carlo AArch64 `yield` contribution.

## Live OpenAI OAuth results

Live checks use isolated temporary workspaces and configuration roots. Credentials, request bodies, and private provider payloads are not recorded in this document.

| Surface | Result | Evidence exercised |
|---|---|---|
| Print mode | Pass | Text generation, file reads, edits, approved process execution, reasoning, and restart/resume |
| Interactive TUI | Pass | Real terminal startup, prompt submission, streaming response, and clean exit |
| RPC | Pass | Streaming, permission reply, user-question reply, attachment input, and cancellation paths |
| Coding tools | Pass | Read, search, list, patch, shell, web fetch/search, skill loading, MCP, plugin, and LSP tool dispatch |
| Task subagents | Pass | Built-in and custom foreground agents; custom background agent start, child-session persistence, and shutdown join |
| Sessions | Pass | Persistent session creation, tool records, private reasoning replay state, process restart, and `--continue` |

OpenAI continuation now persists each assistant turn as one ordered session-v4 transaction. Streaming and non-streaming capture preserve commentary/final text phase, reasoning, function-call identity, and exact emitted order; restart replay reconstructs the same native Responses items. A live OpenAI OAuth check on 2026-07-18 with `gpt-5.6-luna` emitted and persisted `reasoning` → commentary text → `read_file` in one turn, then final-answer text after the bound tool result; RPC validation, public projection, and `--continue` replay all succeeded. Other legal interleavings are additionally covered by deterministic parser, persistence, restart, privacy, and request-serialization tests because a live provider cannot be required to emit every ordering on demand.

## Offline qualification

- Default build and CTest run normally.
- Sanitizer build and CTest run with two jobs.
- Deterministic full-binary coding smoke covers read, grep, list, patch, shell, permissions, persistence, and replay.
- ACP subprocess and official-SDK interop are credential-free.
- MCP, plugin, fake-LSP, RPC, packaging, Kitty-image, OSC-8, and tmux terminal smokes are available without provider spend.
- The frontend F0-F8 offline closure is complete as of 2026-07-23; see the [frontend roadmap](frontend.md) for the milestone record. This is offline evidence only and adds no live-provider claim.
- The tmux smoke is split into 14 isolated scenarios and can run in parallel:

```sh
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --jobs 14 -R '^ava_tui\.tmux_smoke_'
```

Four additional gated direct PTY/protocol CTests cover Kitty, iTerm2, terminal lifecycle, and OSC 8/OSC 52. See [TESTING.md](../TESTING.md) for the current commands and prerequisite behavior.

## Defects found while dogfooding

Dogfooding found and fixed:

- A headless visual pass replaced cyan-heavy built-in hierarchy with neutral muted roles and blue focus/actions, made the automatic rail progressive (`144x16` for actionable activity/changes and `176x16` for idle metadata), capped and centered ordinary conversation/composer/prompt content at 120 columns on wider terminals, gave selectors/questions a quiet backdrop, shortened only proven workspace-path aliases in expanded cards, and kept hidden-rail reasoning changes visible without expanding the footer. Automatic-rail main panes, the full-width `/sidebar` drawer, custom/plain themes, and canonical copied tool data remain unchanged.
- Expanded denied shell cards no longer repeat the raw structured permission status alongside the curated permission audit; unrelated permissioned-shell failures and output remain visible and copyable.
- `/new` lifecycle receipts are title-first, retain the created and previous IDs exactly once for actionability, and do not repeat the active session ID on the switched line.
- Background subagents now use the model-facing `job` controls for list/status/wait/result/cancel; `/jobs` provides show/wait/result/cancel/promote, and RPC exposes the corresponding controls.
- Strict task-subagent schemas rejected omitted default values.
- Task permission records were rejected by session replay validation.
- OpenAI tool calls were replayed as synthetic text rather than native Responses items.
- OpenAI reasoning-plus-tool continuation needed private native reasoning-item replay.
- OpenAI complete function arguments required lifecycle reconciliation.
- Separate reasoning/text/tool persistence lost legal interleaving and assistant phase across restart; session-v4 now commits one ordered assistant-output transaction and binds tool results to exact function items.
- Crash/cancellation windows could strand staged turns or unresolved committed calls; lease-gated suffix recovery and non-reexecuting interrupted-result reconciliation now close those histories safely.

Current workflow controls:

- Background jobs have public status, wait, result, and cancel controls through `/jobs` and RPC.
- Sealed command plans use a fixed trusted command path rather than inheriting arbitrary user `PATH`; approved development commands follow the contained-command policy.
- The built-in `clangd` recipe is default-off and requires exact global opt-in; it uses only an already-installed, identity-checked executable. It is the sole automatic LSP recipe; every other server requires explicit configuration.

## Linux release closure boundary

The pinned `utils` revision is `ce73eaf`; Carlo's MIT relicensing commit is `adee705`. AVA's qualified x86_64/x64 release uses the MIT-licensed Carlo-owned `utils` paths and defines `MIT_LICENSE_ONLY`, which makes accidental inclusion of the guarded BSD `FunctionView.h` and `threading/MpscQueue.h` fail compilation. This is not a claim that every `utils` path has one license: the AArch64 `yield` branch in `cpu_relax.h` remains attributed to Long Wong and is not qualified pending provenance or replacement.

`scripts/package-linux.sh --require-release-qualified` requires a clean source-built x86_64 artifact, matching initialized gitlinks, expected license evidence, and allowlisted host ELF dependencies. The packaged `THIRD_PARTY_NOTICES.md` and `PROVENANCE.json` state the boundary. Supplied-binary packaging remains available but is explicitly unqualified.
