# AVA Architecture Plan

This is a product architecture plan, not implementation code. It defines the first shape of the C++ system so implementation can stay small.

## Initial Shape

AVA starts as one native CLI binary with internal modules.

No separate server process in version 0. Design interfaces so a server can be added later without rewriting the core.

## Core Modules

### cli

Responsibilities:

- Parse arguments.
- Select mode: interactive, print, resume, session.
- Resolve project directory.
- Create runtime services.
- Start the agent loop.

### config

Responsibilities:

- Load global and project config.
- Load context files.
- Load prompt templates and skills later.
- Produce immutable config snapshots for a session.

### session

Responsibilities:

- Create session IDs.
- Append messages, tool calls, tool results, permission events, and summaries.
- Resume sessions.
- Expose active transcript to the agent loop.
- Keep full history even after compaction.

### provider

Responsibilities:

- Convert AVA messages/tools to provider requests.
- Stream provider events back into AVA events.
- Normalize tool calls.
- Track usage.
- Handle cancellation and context overflow.

### agent

Responsibilities:

- Own the turn loop.
- Build prompts.
- Submit provider requests.
- Dispatch tool calls.
- Feed tool results back to the model.
- Stop on completion, cancellation, or unrecoverable error.

### tools

Responsibilities:

- Register built-in tools.
- Validate tool inputs.
- Call permission service before side effects.
- Return bounded structured results.

### permissions

Responsibilities:

- Evaluate allow/ask/deny rules.
- Persist user approvals later.
- Present permission requests to interactive UI.
- Reject unavailable asks in non-interactive mode.

### filesystem

Responsibilities:

- Normalize paths.
- Enforce project boundary policy.
- Perform reads and writes.
- Apply atomic mutations where practical.
- Centralize symlink handling.

### process

Responsibilities:

- Execute commands.
- Enforce timeouts.
- Capture stdout/stderr.
- Kill process trees.
- Centralize shell invocation.

### tui

Responsibilities:

- Render transcript.
- Capture user input.
- Display tool calls and results.
- Display permission prompts.
- Support abort.

## Runtime Dependency Direction

Allowed direction:

- `cli` depends on all top-level services for wiring.
- `agent` depends on `provider`, `tools`, `session`, `config`, and `permissions`.
- `tools` depend on `filesystem`, `process`, and `permissions`.
- `tui` depends on `session`, `agent`, and `permissions` through narrow interfaces.
- `provider` does not depend on `tools` implementation.
- `filesystem` and `process` do not depend on agent/session/provider.

Forbidden direction:

- Tools calling TUI directly.
- Provider calling tool implementations directly.
- Filesystem/process reading config directly after initialization.
- Global mutable singleton state.

## Event Model

Use explicit events between long-running components.

Important event types:

- `user.message`
- `assistant.delta`
- `assistant.message.completed`
- `tool.call.started`
- `tool.call.completed`
- `tool.call.failed`
- `permission.requested`
- `permission.resolved`
- `session.compacted`
- `agent.cancelled`
- `agent.failed`

The session store persists events or normalized records derived from events.

## Data Formats

Prefer stable, inspectable formats:

- Config: TOML or JSON.
- Sessions: JSONL.
- Logs: JSONL.
- Prompt templates and skills: Markdown.

The exact schemas should be written before implementation.

## C++ Implementation Constraints

Follow `docs/engineering/cpp-safety-rules.md`.

Additional product-specific constraints:

- Use value types for request/response models where possible.
- Use RAII handles for sessions, process execution, and cancellation scopes.
- Use explicit result types for recoverable failures.
- Avoid callback webs across the codebase; centralize streaming adapters.
- Keep async simple until the interactive loop forces more sophistication.

## First Dependency Candidates

These are candidates, not final decisions:

- CMake for builds.
- A small HTTP client library with TLS support.
- A JSON library.
- A terminal rendering/input library only if raw terminal handling becomes too costly.
- `libgit2` only later if shelling to git is insufficient.
- LSP support later, likely via JSON-RPC over subprocess.

Dependency rule: choose boring, maintained dependencies and hide each behind an AVA-owned interface.

## Server Later

Client/server architecture is useful, but not version 0.

If AVA later adds a server, it should expose the same core services:

- Projects.
- Sessions.
- Messages.
- Tool calls.
- Permission prompts.
- File search/read/status.
- Events stream.

This is why core modules should not know whether the caller is TUI, print mode, or future HTTP client.

## Architecture Rule

AVA starts as a single binary, but the core must be separated from terminal presentation. That keeps the MVP lean without trapping the product in a TUI-only design.
