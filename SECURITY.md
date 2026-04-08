# Security Policy

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 3.3.x   | Yes                |
| < 3.3   | No                 |

Only the latest release in the 3.3.x series receives security updates.

## Reporting a Vulnerability

**Do not open a public issue for security vulnerabilities.**

Instead, send an email to **security@asf-group.dev** with:

- A description of the vulnerability
- Steps to reproduce (or a proof of concept)
- The affected component(s) and version(s)
- Any potential impact assessment

### What to Expect

- **Acknowledgment**: Within 48 hours of your report.
- **Initial assessment**: Within 7 days. We will confirm whether the report is accepted and provide an estimated timeline for a fix.
- **Fix and disclosure**: We aim to release a patch within 30 days of confirming a vulnerability. We will coordinate disclosure timing with the reporter.

If the report is declined (e.g., out of scope or not reproducible), we will explain why.

## Security Features

AVA includes several layers of security for agent-driven tool execution:

- **Command sandbox** (`crates/ava-sandbox/`): Isolates shell commands using `bwrap` (Linux) and `sandbox-exec` (macOS). Install-class commands are routed through the sandbox via middleware (priority 3).
- **Permission levels** (`crates/ava-permissions/`): Standard and AutoApprove modes. Even in AutoApprove mode, critical commands (e.g., `rm -rf /`, `sudo`, fork bombs) are blocked by a 9-step inspector.
- **Command classification** (`crates/ava-permissions/`): Bash commands are classified by risk using 15+ pattern rules. Commands are tagged with one of 8 safety tags and assigned a risk level (5 tiers).
- **Risk-aware approval UI**: The TUI presents tool calls with risk context, allowing users to review and approve before execution.
- **Path safety**: File operations are validated to prevent traversal and unauthorized access outside the project scope.
- **Checkpoint recovery**: Error-recovery middleware (priority 15) creates checkpoints before destructive actions, enabling rollback.

## Scope

### In Scope

- The Rust CLI binary and all crates under `crates/`
- Agent runtime (tool execution, permission checks, sandboxing)
- Session and memory storage (`crates/ava-session/`, `crates/ava-memory/`)
- Configuration and credential handling (`crates/ava-config/`)
- Desktop backend (Tauri commands in `src-tauri/`)
- Custom tool execution (TOML tools in `~/.ava/tools/` and `.ava/tools/`)

### Out of Scope

- Third-party LLM provider APIs (Anthropic, OpenAI, Google, etc.) -- report issues directly to those providers
- Third-party MCP servers and their tool implementations
- Vulnerabilities in upstream dependencies (report to the upstream project, but let us know if AVA is affected)
- Social engineering or phishing attacks
- Denial of service against hosted LLM endpoints

## Credential Storage

AVA supports user-local plaintext credential files for compatibility, but keychain/encrypted or environment-variable paths are preferred when available. If `~/.ava/credentials.json` is used, it should be readable only by the owning user (`chmod 600`). AVA does not transmit credentials to any party other than the configured LLM provider endpoints.
