# Cline CLI & Standalone

> Analysis of Cline's CLI interface and standalone deployment model

---

## Overview

Cline provides three deployment targets that share the same core controller and task execution engine:

1. **VS Code Extension** (primary) - Main UI with webview
2. **CLI** - Terminal-based interface using React Ink
3. **Standalone** - Headless service using gRPC (ProtoBus)

---

## CLI Architecture (`cli/`)

### Entry Point (`src/index.ts`)

**Commands Available:**

| Command | Purpose | Key Options |
|---------|---------|-------------|
| `cline [prompt]` | Interactive/task mode | `-a`, `-p`, `-y`, `-m`, `--thinking`, `--config` |
| `cline task <prompt>` | Run new task | Same + `-i` for images |
| `cline history` | List task history | `-n` (limit), `-p` (page) |
| `cline config` | Show configuration | `--config` |
| `cline auth` | Setup authentication | `-p`, `-k`, `-m`, `-b` |
| `cline version` | Show CLI version | - |
| `cline update` | Check for updates | `-v` (verbose) |
| `cline dev log` | Open log file | - |

### Execution Modes

1. **Interactive Mode**: Default when no prompt → shows welcome UI
2. **Task Mode**: `cline "prompt"` → runs task directly
3. **Plain Text Mode**: Detects TTY redirection → outputs without Ink UI
4. **ACP Mode**: `--acp` flag → Agent Client Protocol for editor integration
5. **Auth Setup**: Quick setup with `-p provider -k apikey -m modelid`

### Key Features

- Full image support (`@/path/image.png` or `--images` flag)
- Extended thinking support (`--thinking` → 1024 token budget)
- YOLO mode (`--yolo` → auto-approve all actions)
- Workspace-aware (default to current directory or `-c --cwd`)
- Config directory override (`--config` for custom data dir)
- Stdin handling (piped stdin prepended to prompt)

---

## ACP Mode (Agent Client Protocol)

**Purpose:** Implements ACP 0.13.1 SDK for programmatic agent use

**Structure:**
- `ClineAgent` - Core agent implementation
- `AcpAgent` - Thin wrapper bridging stdio to ClineAgent
- `ClineSessionEmitter` - TypeScript EventEmitter for session events

**Communication:**
```
Client (VS Code, Sublime, etc.)
    ↓ [JSON-RPC over stdio]
cline --acp
    ↓ [ndJsonStream]
AcpAgent (stdio wrapper)
    ↓
ClineAgent (core logic)
    ↓
Controller → Task Engine
```

**Supported ACP Methods:**
- Initialize/List Resources
- Create Session
- List Capabilities
- Call Tools
- Streaming responses

---

## Standalone Architecture (`src/standalone/`)

**Purpose:** Headless service deployment for enterprise/self-hosted scenarios

### Key Components

**cline-core.ts** (150+ lines):
- Main service entry point
- Parses CLI args (--config, --port, --hostBridgePort)
- Initializes context and StateManager
- Starts gRPC server (ProtoBus)
- Handles graceful shutdown

**protobus-service.ts** (116 lines):
- Creates gRPC server on port 26040 (default)
- Adds proto-generated handlers
- Includes gRPC reflection service
- Health check endpoint

**lock-manager.ts**:
- SQLite-based instance registry
- Tracks active instances
- Prevents duplicates

### Deployment Model

```
Standalone Cline Core (gRPC ProtoBus)
    ↓ [gRPC]
External Host Bridge (separate service/UI)
    ↓ [Custom protocol]
Client Applications (web UI, Electron, etc.)
```

### Port Configuration

```
--port 26040         # ProtoBus gRPC server
--hostBridgePort     # External host bridge

Environment variables:
PROTOBUS_ADDRESS
HOST_BRIDGE_ADDRESS
```

---

## Host Bridge Adapters (`cli/src/controllers/`)

**CliDiffServiceClient** - Diff operations for terminal
- Most operations are no-ops (no visual diff editor)
- Multi-file diff shows summary

**CliEnvServiceClient** - Environment operations
- Clipboard read/write (in-memory)
- Platform detection
- Telemetry settings

**CliWindowServiceClient** - Window operations
- Modal dialogs via console
- Open external URLs
- Focus management stubs

**CliWorkspaceServiceClient** - Workspace operations
- File system operations
- Workspace root management

---

## Comparison: VS Code vs CLI vs Standalone

| Aspect | VS Code Extension | CLI | Standalone |
|--------|------------------|-----|------------|
| **Deployment** | VSCode native | npm globally | Self-hosted service |
| **UI Rendering** | Webview (React) | Ink (terminal) | None (via client) |
| **Process Model** | Extension process | Single Node.js | gRPC service |
| **Communication** | VSCode message passing | Command routing | gRPC + Host Bridge |
| **Persistence** | VSCode globalState | File system | StateManager |
| **Auth Flow** | VSCode AuthenticationProvider | OAuth via localhost | External AuthHandler |
| **Terminal Commands** | Shell integration API | Standalone manager | Delegated |
| **File Editing** | VS Code TextEditor | Direct file I/O | Delegated |
| **Diff Viewing** | VSCode native diff | No-op | External handler |

---

## Core Reuse

All three implementations share:
- `src/core/controller/` - Task orchestration
- `src/core/task/` - AI interaction and tool execution
- `src/core/api/` - 40+ LLM provider integrations
- `src/core/prompts/` - System prompt variants
- `src/shared/` - Types, storage, utilities
- Proto definitions for message types

---

## File Organization

```
CLI:
  cli/src/
    ├── index.ts (812L) - Main entry, command routing
    ├── acp/ - Agent Client Protocol
    │   ├── index.ts - runAcpMode entry
    │   ├── AcpAgent.ts - stdio wrapper
    │   ├── ClineAgent.ts - core agent
    │   └── ClineSessionEmitter.ts - event handling
    ├── components/ - React Ink UI (50+ components)
    ├── controllers/ - Host bridge adapters
    ├── context/ - React contexts
    ├── hooks/ - React hooks for terminal
    └── utils/ - CLI utilities

Standalone:
  src/standalone/
    ├── cline-core.ts - Main service
    ├── protobus-service.ts - gRPC setup
    ├── hostbridge-client.ts - external bridge
    ├── lock-manager.ts - instance registry
    └── vscode-context.ts - context init

  standalone/runtime-files/vscode/
    ├── vscode-stubs.js - VSCode API stubs
    └── vscode-impls.js - stub implementations
```

---

## Notable Features for Estela

### 1. ACP (Agent Client Protocol)
Programmatic agent interface for editor integration.

### 2. Plain Text Mode
Automatic fallback for non-TTY environments and piped input.

### 3. Stdin Integration
Seamless stdin piping with prompt prepending.

### 4. Standalone Service
Headless deployment with gRPC + Host Bridge.

### 5. Image Attachments
Inline `@/path/image.png` syntax + `--images` flag.

### 6. Extended Thinking
`--thinking` flag with configurable token budgets.

### 7. YOLO Mode
Auto-approval mode with optional timeout.

### 8. Multi-provider Quick Setup
`cline auth -p provider -k key -m model` pattern.

### 9. Workspace-aware Context
Intelligent switching between directories.

### 10. Config Directory Override
`--config` flag for custom data storage paths.
