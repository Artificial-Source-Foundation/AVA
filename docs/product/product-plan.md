# AVA Product Plan

AVA is a lean C++ agentic coding tool. It should feel as fast and direct as pi, while carrying the practical built-in tool quality expected from OpenCode.

This document defines the product before implementation. It is intentionally biased toward a small first version.

## Product Thesis

AVA is a native coding agent for developers who want:

- Fast startup and low idle overhead.
- A terminal-first workflow.
- Strong file, shell, search, and edit tools.
- Clear safety controls around destructive actions.
- Durable sessions that can be resumed, inspected, compacted, and shared later.
- Provider flexibility without turning provider integration into the core product.

AVA should not start as a platform, plugin marketplace, desktop app, web app, or multi-client server. Those can exist later only if the core loop is excellent.

## Reference Products

### pi-mono Lessons

Useful product ideas:

- Minimal terminal coding harness.
- Small default tool set: read, write, edit, bash, plus search/list helpers.
- Sessions stored locally and resumable.
- Session branching and compaction as first-class workflow concepts.
- Context files loaded from project and user scopes.
- Prompt templates and skills as simple filesystem resources.
- Extensibility is powerful, but the core stays small.

What AVA should not copy initially:

- Full TypeScript extension runtime.
- Package manager for third-party agent packages.
- Deep theme/custom UI system.
- Broad SDK surface before the CLI is stable.

### OpenCode Lessons

Useful product ideas:

- Agent modes with different permissions, especially build vs plan.
- Strong built-in tools beyond the minimal four: glob, grep, apply patch, web fetch, question, LSP/code intelligence, task/subagent.
- Explicit permission system with allow, deny, and ask decisions.
- Client/server architecture is useful long term for multiple clients.
- Project/session API shape is useful long term for external integrations.
- Out-of-the-box LSP support is a meaningful differentiator.

What AVA should not copy initially:

- Full client/server architecture in version 0.
- Desktop app.
- Web app.
- MCP as a core requirement.
- Broad plugin/runtime system before core safety and sessions are stable.

### Old Rust AVA Reference

Old Rust AVA is a historical reference for tool card and permission modal ideas only; do not restore its architecture or code.

## Design Principles

- Native first: AVA ships as one fast C++ binary.
- Terminal first: the CLI is the product until proven otherwise.
- Small core, strong defaults: no weak placeholder tools just to claim a feature.
- Filesystem as interface: config, skills, prompts, sessions, and logs should be inspectable files.
- Safety is product behavior, not just implementation detail.
- Provider-agnostic but not provider-obsessed: start with one or two high-quality providers, then expand.
- Avoid magic: tool actions should be visible, resumable, and explainable.
- No backward compatibility burden before the first real release.

## First-Class Workflows

- Ask AVA to inspect a codebase and explain it.
- Ask AVA to edit code using safe file tools.
- Ask AVA to run commands with timeouts and clear output limits.
- Ask AVA to search files quickly with glob and grep.
- Ask AVA to plan without changing files.
- Resume a previous session.
- Compact long context while retaining the full local history.
- Review tool calls and permission decisions.

## MVP Scope

MVP means a usable coding agent, not a demo.

### CLI Modes

- `ava`: interactive terminal session.
- `ava "prompt"`: one-shot prompt in current directory. Deferred after 0.1.
- `ava --print "prompt"`: non-interactive output for scripts. Deferred after 0.1.
- `ava --continue`: resume latest session for current project.
- `ava --session <id-or-path>`: open a specific session.
- `ava --no-session`: ephemeral mode. Deferred after 0.1.

### Built-In Agent Modes

- `build`: can read, search, edit, write, and request shell execution.
- `plan`: read/search only by default; shell requires approval; file mutation denied.

Subagents are not MVP. A later `task` tool can spawn isolated AVA workers only after sessions and permissions are stable.

### MVP Tools

- `read`: read bounded file ranges with line numbers.
- `write`: create or overwrite files through the safe file layer.
- `edit`: exact old/new replacement with validation.
- `apply_patch`: structured patch application for multi-file edits.
- `glob`: fast file discovery.
- `grep`: fast content search.
- `bash`: argv/workdir-aware command execution with timeout, output limit, and permission policy.
- `question`: ask the user for structured clarification when needed. In 0.1 this tells the assistant to ask directly; modal prompts are deferred.

### Post-MVP Tools

- `lsp`: definitions, references, diagnostics, symbols.
- `webfetch`: fetch a URL as markdown/text.
- `task`: launch a bounded subagent process.
- `todo`: optional session-local planning aid, only if it demonstrably improves outcomes.

## Permission Model

AVA should ship with permissions from the start.

Permission actions:

- `allow`: execute without asking.
- `ask`: request user approval. In 0.1, backend tools deny ask decisions until a prompt UI exists.
- `deny`: reject and explain the rule.

Initial permission categories:

- `file.read`
- `file.write`
- `file.delete`
- `shell.run`
- `shell.destructive`
- `network.fetch`
- `external.directory`

Default policy:

- Reads inside the project are allowed.
- Writes inside the project are allowed in build mode unless risky and denied in plan mode except planning markdown.
- Deletes ask, even in build mode.
- Shell commands use a conservative built-in classifier; ask decisions are denied until prompt UI exists.
- Clearly destructive shell commands are denied or require explicit confirmation.
- Access outside the project asks or denies depending on command category.

## Session Model

Sessions should be append-only local files.

Minimum session data:

- Session ID.
- Project directory.
- Created and updated timestamps.
- Active model/provider.
- User messages.
- Assistant messages.
- Tool calls.
- Tool results.
- Permission requests and replies.
- Compaction summaries.
- Token and cost estimates when available.

Storage target:

- Global: `$XDG_STATE_HOME/ava/sessions/`, falling back to `~/.local/state/ava/sessions/`.
- Project-local metadata: `.ava/` only when project-specific files are needed.

Branching can be designed early but implemented after MVP. The first version can support linear resume; branch/tree navigation can follow.

## Configuration

Config should be plain files and merge from global to project scope.

Suggested locations:

- `$XDG_CONFIG_HOME/ava/` or `~/.config/ava/`
- `.ava/config.toml`
- Global and project `AGENTS.md` files discovered from repository root to current directory.
- `.ava/SYSTEM.md` for project-specific system prompt replacement.
- `.ava/prompts/` for prompt templates.
- `.ava/skills/` for skills.

Use TOML or JSON, but choose one early and keep the schema small.

## Provider Strategy

MVP should support the fewest providers needed to prove the agent loop.

Initial providers:

- OpenAI Responses API or Chat Completions API.

Provider abstraction requirements:

- Streaming text.
- Tool calls.
- Tool results.
- Usage accounting when provided.
- Cancellation.
- Context overflow detection.

Avoid building a large model registry before the first loop works.

## TUI Strategy

MVP interface should be usable but not fancy.

Required:

- Message transcript.
- Tool call display with collapsible output later.
- Input editor with multiline support.
- Abort current turn.
- Show current model, directory, session, and mode.
- The slash-command palette owns input collection, candidate display, and rendering only; parsing, authorization, session mutation, tool execution, and mode changes stay in app command handlers outside the TUI.

Deferred:

- Theme system.
- Complex overlays.
- Session tree UI.
- Images and clipboard handling.
- Desktop/web UI.

## Non-Goals For Version 0

- Plugin system.
- Package ecosystem.
- Desktop app.
- Web app.
- MCP built in.
- Multi-client server.
- Remote mobile control.
- Full SDK.
- Background autonomous daemon.
- Auto-commits.
- Sandboxing beyond explicit process/file policy.

## Product Milestones

### M0: Planning And Architecture

- Product plan.
- Tool specification.
- Session format specification.
- Permission policy specification.
- Provider streaming contract.
- C++ safety and dependency rules.

### Implemented 0.1 Milestones

- Provider/model/auth foundation.
- OpenAI request/response contract and curl-backed HTTP transport.
- Sequential agent tool loop.
- Read, write, edit, apply_patch, glob, grep, bash, question tools.
- Append-only XDG session store with resume by latest or ID.
- Build/plan permission evaluator.
- Simple TUI composer with non-TTY line-shell fallback.
- Tests and sanitizer builds.

### Planned 0.2 Focus

- Polish the interactive TUI until it is comfortable for daily coding sessions.
- Use OpenCode as the primary TUI visual/interaction reference; use pi-mono and old Rust AVA only as secondary references for compact tool cards and permission prompts.
- Decide whether to keep the custom terminal path or adopt FTXUI before adding a new TUI dependency.
- Make agent tool calls, results, failures, and truncation visible in the transcript.
- Add OpenCode-like slash command palette discovery and readable thinking/progress visibility.
- Add interactive permission prompts for `ask` decisions while keeping non-interactive mode fail-closed.
- Verify the existing 0.1 tools through real agent workflows before adding more providers or automation modes.
- Keep additional providers, MCP/plugins, subagents, LSP, web fetch, and automation CLI modes deferred to later versions.

### Deferred Beyond 0.2 / 0.3+ Backlog

These items were deferred from 0.1 and remain post-0.2 unless the 0.2 scope explicitly reclassifies them. See `docs/versions/0.2.md` for the 0.2 backlog boundary and hard deferrals.

- Print mode and the JSONL RPC MVP are implemented; richer protocol controls remain on the backend roadmap.
- Persistent allow/deny rule management.
- Diff previews and richer patch UI.
- Structured skills and prompt templates beyond `AGENTS.md`.
- Provider-generated and automatic compaction.
- LSP diagnostics/symbols/definitions.
- Web fetch.
- Session tree/fork/clone workflows.

## Core Product Rule

AVA should earn every feature by making the coding loop safer, faster, or more controllable. If a feature mostly makes AVA look bigger, it waits.
