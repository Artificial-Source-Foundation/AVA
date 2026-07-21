# AVA dogfood qualification

Status: active qualification on `develop` (2026-07-18).

AVA is ready for regular repository dogfooding. Public binary release remains blocked by the linked ai-utils licensing issue described below.

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
- The tmux smoke is split into 13 isolated scenarios and can run in parallel:

```sh
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --jobs 13 -R '^ava_tui\.tmux_smoke_'
```

## Defects found while dogfooding

Dogfooding found and fixed:

- Strict task-subagent schemas rejected omitted default values.
- Task permission records were rejected by session replay validation.
- OpenAI tool calls were replayed as synthetic text rather than native Responses items.
- OpenAI reasoning-plus-tool continuation needed private native reasoning-item replay.
- OpenAI complete function arguments required lifecycle reconciliation.
- Separate reasoning/text/tool persistence lost legal interleaving and assistant phase across restart; session-v4 now commits one ordered assistant-output transaction and binds tool results to exact function items.
- Crash/cancellation windows could strand staged turns or unresolved committed calls; lease-gated suffix recovery and non-reexecuting interrupted-result reconciliation now close those histories safely.

Remaining workflow gaps:

- Background jobs can start and persist child sessions, but there is no public status, wait, result, or cancel command.
- The permissioned shell uses a fixed PATH; bare user-installed build commands may be unavailable even after approval.
- Real LSP behavior still needs a zero-configuration server recipe; deterministic fake-server coverage passes.

## Public release blocker: ai-utils licensing

AVA currently links 23 object files from the `utils/` (`ai-utils`) submodule directly into the executable. The submodule license is GPLv3. The linked `itoa.cxx`, `threading/aithreadid.cxx`, `threading/Semaphore.cxx`, and `threading/SpinSemaphore.cxx` files explicitly carry AGPLv3-or-later notices. AVA cannot truthfully ship that combined executable as MIT-only.

AVA's intentional use is narrow: RPC output synchronization uses the MIT-licensed `threadsafe` wrapper, whose headers import ai-utils. The intended fix is to replace that wrapper with AVA-owned standard-library mutex code, stop linking `utils_ObjLib`, verify the release binary no longer contains those objects, and add a packaging license gate.
