# ava-cli-providers

> External CLI agent integration — wraps CLI-based AI agents (Claude Code, Codex, Aider, etc.) as LLM providers.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `CLIAgentLLMProvider` | Implements `LLMProvider` trait for external CLI agents |
| `CLIAgentLLMProvider::new()` | Create provider from config, optional model override, and yolo mode |
| `CLIAgentLLMProvider::cancel()` | Cancel a running agent execution |
| `CLIAgentRunner` | Manages CLI agent lifecycle and command execution |
| `CLIAgentRunner::new()` | Create runner with configuration |
| `CLIAgentRunner::is_available()` | Check if CLI binary is installed |
| `CLIAgentRunner::version()` | Get CLI version string |
| `CLIAgentRunner::run()` | Execute agent synchronously, return result |
| `CLIAgentRunner::stream()` | Execute with streaming events via channel |
| `CLIAgentRunner::cancel()` | Signal cancellation token |
| `RunOptions` | Execution options (prompt, cwd, model, yolo, allowed_tools, timeout, env) |
| `CLIAgentConfig` | Complete CLI agent configuration (flags, capabilities, version command) |
| `PromptMode` | Flag-based (`-p`) or Subcommand-based (`exec`) prompt passing |
| `CLIAgentResult` | Execution result (success, output, exit_code, events, tokens, duration) |
| `CLIAgentEvent` | Streaming events: Text, ToolUse, ToolResult, Error, Usage |
| `TokenUsage` | Input/output token counts from Usage events |
| `AgentRole` | Engineer, Reviewer, Subagent — for tier-specific prompts and tools |
| `execute_with_cli_agent()` | Execute with role-appropriate settings and streaming support |
| `discover_agents()` | Auto-detect installed CLI agents on system |
| `create_providers()` | Convert discovered agents to LLMProvider instances |
| `DiscoveredAgent` | Discovered agent with name, binary, version, config |
| `builtin_configs()` | Built-in configs for claude-code, gemini-cli, codex, opencode, aider |
| `messages_to_prompt()` | Convert AVA message history to single CLI prompt string |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports all public types and modules (20 lines) |
| `provider.rs` | `CLIAgentLLMProvider` — implements `LLMProvider` trait, message-to-prompt conversion (285 lines) |
| `runner/mod.rs` | `CLIAgentRunner` struct and public API (is_available, version, run, stream, cancel) (241 lines) |
| `runner/args.rs` | Command line argument building from config and options (70 lines) |
| `runner/execution.rs` | Core execution logic: spawn, stream parsing, timeout handling, result collection (190 lines) |
| `config.rs` | Configuration structs: `CLIAgentConfig`, `PromptMode`, `CLIAgentResult`, `CLIAgentEvent`, `TokenUsage` (152 lines) |
| `configs.rs` | Built-in agent configurations for 5 CLI agents with feature flags (182 lines) |
| `discovery.rs` | Parallel agent discovery with version checking (201 lines) |
| `bridge.rs` | Role-based execution (Engineer/Reviewer/Subagent) with tier prompts and timeouts (169 lines) |

## Dependencies

Uses: ava-llm, ava-types

Used by: ava-praxis (optional, via `cli-providers` feature)

## Key Patterns

- **LLMProvider trait**: Wraps CLI agents to integrate with AVA's provider system alongside native LLM providers
- **Stream-json protocol**: Supports structured JSON events (text, tool_use, tool_result, usage) from agents like Claude Code
- **Graceful degradation**: Agents without stream-json support fall back to plain text output
- **Tool scoping**: Configurable per-tier tool restrictions (engineer vs reviewer vs subagent) for security
- **Parallel discovery**: Spawns concurrent version checks for all builtin configs using tokio::spawn
- **Cancellation**: Uses `tokio_util::sync::CancellationToken` for cooperative cancellation of long-running agents
- **Prompt construction**: `messages_to_prompt()` builds tiered prompts with system context, conversation transcript, and primary task
- **Role-based execution**: `AgentRole` enum with tier-specific prompts, timeouts (600s/300s/120s), and tool scopes
- **Binary availability**: Version command execution determines if agent is installed before offering as provider
- **Yolo mode**: Maps to agent-specific auto-approve flags (`--dangerously-skip-permissions`, `--full-auto`, `--yes-always`)
