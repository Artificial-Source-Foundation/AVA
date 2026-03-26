# ava-acp — Agent Client Protocol

Replaces the old `ava-cli-providers` crate. Provides a standardized way to integrate external CLI agents (Claude Code, OpenCode, Codex, Aider, Gemini CLI) via JSON-RPC over stdio.

## Key Types

- **`AgentQuery`** — prompt, cwd, max_turns, permission_mode, allowed/disallowed tools
- **`AgentMessage`** — streaming events: text, thinking, tool use/result, errors
- **`AgentResult`** — final output, session_id, cost, usage
- **`AgentTransport` trait** — `query()`, `interrupt()`, `cancel()` for any agent backend

## Adapters

| Adapter | Protocol | Agents |
|---------|----------|--------|
| `ClaudeSdkAdapter` | Agent SDK v1 (`--output-format stream-json`) | Claude Code |
| `LegacyCliAdapter` | Plain text / stream-json | Codex, Aider, OpenCode |

## Factory

`AcpProviderFactory` implements `ProviderFactory` — handles `"acp"` and `"cli"` route prefixes. Agents are spawned on-demand (no startup discovery).

## ACP Server

`--acp-server` flag runs AVA itself as an ACP-compliant agent on stdio, for IDE integration (Zed, etc.).

## Config

Agents configured via `~/.ava/agents.toml` or `.ava/agents.toml`:

```toml
[[agents]]
name = "claude"
binary = "claude"
protocol = "sdk-v1"
```

## Module Map

```
src/
├── lib.rs          — crate root, re-exports
├── protocol.rs     — AgentQuery, AgentMessage, AgentResult, ContentBlock
├── transport.rs    — AgentTransport trait
├── stdio.rs        — StdioProcess (NDJSON subprocess communication)
├── provider.rs     — AcpAgentProvider (implements LLMProvider)
├── factory.rs      — AcpProviderFactory (implements ProviderFactory)
├── server.rs       — ACP server mode (--acp-server)
└── adapters/
    ├── mod.rs
    ├── claude_sdk.rs  — Claude Code Agent SDK adapter
    ├── legacy_cli.rs  — Plain text / stream-json adapter
    └── config.rs      — AgentConfig, AgentProtocol, discovery
```
