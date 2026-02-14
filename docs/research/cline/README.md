# Cline Codebase Analysis

> Comprehensive analysis of the Cline VS Code extension codebase

---

## Overview

Cline is a VS Code extension that provides AI-powered coding assistance. This analysis documents its architecture, patterns, and features to identify what AVA can learn from or adopt.

**Source:** `docs/reference-code/cline/`
**Version Analyzed:** Latest as of February 2026

---

## Document Index

| # | Document | Coverage |
|---|----------|----------|
| 01 | [Core API & Controller](./01-core-api-controller.md) | API handlers, controller orchestration |
| 02 | [Prompts & Permissions](./02-prompts-permissions.md) | System prompts, permission models |
| 03 | [Tasks & Hooks](./03-tasks-hooks.md) | Task execution, lifecycle hooks |
| 04 | [Services](./04-services.md) | Browser, Git, MCP, Providers |
| 05 | [Integrations](./05-integrations.md) | External APIs, connections |
| 06 | [Webview UI](./06-webview-ui.md) | React components, state management |
| 07 | [CLI & Standalone](./07-cli-standalone.md) | CLI commands, standalone app |
| 08 | [Types & Utilities](./08-types-utilities.md) | Type definitions, shared utilities |
| 09 | [Comparison](./COMPARISON.md) | Cline vs AVA feature comparison |

---

## Cline Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      VS Code Extension                       │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │   API    │  │Controller│  │ Prompts  │  │Permissions│    │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘    │
│       │             │             │             │           │
│       └─────────────┴─────────────┴─────────────┘           │
│                          │                                   │
│                   ┌──────┴──────┐                           │
│                   │    Core     │                           │
│                   │   (Task)    │                           │
│                   └──────┬──────┘                           │
│                          │                                   │
│  ┌───────────────────────┼───────────────────────┐          │
│  │                       │                       │          │
│  ▼                       ▼                       ▼          │
│  ┌──────────┐     ┌──────────┐     ┌──────────┐            │
│  │ Services │     │Integrations│   │ Webview  │            │
│  │  - Git   │     │  - MCP   │     │   UI     │            │
│  │  - Browser│    │  - APIs  │     │ (React)  │            │
│  │  - ...   │     │  - ...   │     │          │            │
│  └──────────┘     └──────────┘     └──────────┘            │
└─────────────────────────────────────────────────────────────┘
```

---

## Key Directories

```
cline/
├── src/
│   ├── core/           # Core business logic
│   │   ├── api/        # API request handlers
│   │   ├── controller/ # Main orchestration
│   │   ├── prompts/    # System/tool prompts
│   │   ├── permissions/# Permission checking
│   │   ├── task/       # Task execution
│   │   ├── hooks/      # Lifecycle hooks
│   │   └── ...
│   ├── services/       # Service layer
│   ├── integrations/   # External integrations
│   ├── types/          # TypeScript types
│   ├── shared/         # Shared utilities
│   └── utils/          # Helper functions
├── webview-ui/         # React UI
├── cli/                # CLI implementation
└── standalone/         # Standalone app
```

---

## Analysis Status

| Task | Status | Agent |
|------|--------|-------|
| Core API & Controller | ✅ Complete | a47269d |
| Prompts & Permissions | ✅ Complete | a5d3082 |
| Tasks & Hooks | ✅ Complete | a21a705 |
| Services | ✅ Complete | a9aa637 |
| Integrations | ✅ Complete | adf030a |
| Webview UI | ✅ Complete | a9f8418 |
| CLI & Standalone | ✅ Complete | a67263b |
| Types & Utilities | ✅ Complete | a85b399 |

---

## Key Findings

### What Cline Has That AVA Should Adopt

**Critical:**
1. **Hook System** - 8 lifecycle hooks with subprocess isolation
2. **Checkpoint System** - Shadow Git for change tracking
3. **MCP OAuth** - Full OAuth flow with token refresh
4. **Remote Browser** - WebSocket-based remote Chrome
5. **gRPC Communication** - Type-safe webview-extension messaging

**High Priority:**
6. Chained command validation (per-segment)
7. Dual-phase tool execution (streaming + completion)
8. Stream normalization (unified ApiStreamChunk)
9. Diagnostics tracking (pre/post edit)
10. Virtual scrolling for large histories

### Where AVA is Ahead

1. **Batch Tool** - Execute up to 25 tools in parallel
2. **Multi-Edit Tool** - Multiple sequential edits
3. **Fuzzy Edit Strategies** - 9 strategies for matching
4. **Apply Patch Tool** - Unified diff format
5. **Skill System** - Reusable knowledge modules
6. **Doom Loop Detection** - Prevent infinite loops
7. **Code Search (Exa)** - API documentation search

---

## Architecture Insights

### 40+ Providers
Cline supports 40+ LLM providers through a factory pattern with unified `ApiHandler` interface and stream normalization.

### gRPC-Based Communication
Extension-webview communication uses gRPC/protobuf for strict typing, streaming support, and request recording.

### Hook System
8 lifecycle hooks (TaskStart, TaskResume, TaskCancel, TaskComplete, PreToolUse, PostToolUse, UserPromptSubmit, PreCompact) with subprocess isolation and context modification.

### Checkpoint System
Shadow Git repositories per workspace for AI change tracking without interfering with user's repo.

---

## Recommendations

See [COMPARISON.md](./COMPARISON.md) for detailed feature matrix and implementation roadmap.
