# Changelog

All notable changes to Delta9 will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

#### Developer Experience (DX) Improvements

- **Structured Logger** (`src/lib/logger.ts`)
  - Named component loggers with `getNamedLogger(component)`
  - Hierarchical logging: `[delta9:background] [INFO] Task started`
  - Context injection via `child()` method
  - Integration with OpenCode's `app.log()` when available
  - Graceful fallback to console with formatting

- **Rich Error Handling** (`src/lib/errors.ts`)
  - `Delta9Error` class with code, message, suggestions, and context
  - Predefined error factories for common error cases
  - Type guard `isDelta9Error()` for error handling
  - `toToolResponse()` method for JSON serialization
  - Recovery suggestions included in all errors

- **Context-Aware Hints** (`src/lib/hints.ts`)
  - Hints for empty states (no tasks, no mission, etc.)
  - Tool-specific hint helpers: `getBackgroundListHint()`, `getMissionStatusHint()`
  - Dynamic hints based on system state
  - Guides users to next action

- **Health Diagnostics Tool** (`src/tools/diagnostics.ts`)
  - `delta9_health` tool for system diagnostics
  - Reports SDK status, mission state, background tasks, config validity
  - Verbose mode for detailed task and mission history
  - Health status: healthy/degraded/unhealthy with emoji indicators

- **Enhanced Tool Descriptions**
  - All tools now have detailed descriptions with examples
  - Purpose statements, parameter explanations
  - Related tools listed for discoverability
  - Agent type documentation in `delegate_task`

- **Status Output Enhancements**
  - Emoji status indicators: ⏳ pending, 🔄 running, ✅ completed, ❌ failed, 🚫 cancelled
  - Human-readable duration formatting (ms, s, m, h)
  - Summary lines with counts in `background_list`
  - Pool utilization percentage

#### SDK Integration

- **Real Agent Execution**
  - Integration with OpenCode SDK `client.session.run()`
  - Background agent spawning with proper session management
  - Abort controller support for task cancellation
  - Automatic detection of SDK availability

#### Robustness Improvements

- **Process Cleanup**
  - Graceful shutdown handling
  - Signal handlers for SIGINT, SIGTERM, SIGQUIT
  - Automatic cleanup on process exit

- **Stale Task Detection**
  - 30-minute TTL for completed/failed tasks
  - Automatic pruning on task list access
  - Manual cleanup via `background_cleanup` tool

- **Background Task Pool**
  - Concurrency limiting (3 parallel tasks)
  - FIFO task queue
  - State tracking: pending/running/completed/failed/cancelled

### Changed

- `background_list` now returns emoji status indicators and durations
- `delegate_task` includes detailed agent type documentation
- Error responses now include recovery suggestions
- Logging uses structured format with component names

### Fixed

- Fixed timestamp arithmetic using ISO strings instead of Date objects
- Fixed duplicate property names in hints module
- Fixed type compatibility between logger and background-manager

## [0.1.0] - 2026-01-20

### Added

- Initial plugin scaffold
- Mission state management (`MissionState` class)
- Configuration system with Zod validation
- 33 custom tools across 8 categories:
  - Mission management (8 tools)
  - Delegation (2 tools)
  - Background tasks (4 tools)
  - Council (3 tools)
  - Memory (5 tools)
  - Validation (4 tools)
  - Checkpoints (4 tools)
  - Diagnostics (1 tool)
- Background task manager with concurrency control
- TypeScript strict mode
- Vitest test setup

### Documentation

- Full specification (`docs/spec.md`)
- API reference (`docs/delta9/api.md`)
- Development guide (`docs/delta9/development.md`)
- Architecture overview (`docs/delta9/architecture.md`)
- Agent roster (`docs/delta9/agents.md`)
