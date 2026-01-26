# Delta9

> Strategic AI Coordination for Mission-Critical Development

Delta9 is an [OpenCode](https://opencode.ai) plugin that implements a hierarchical **Commander + Council + Operators** architecture for complex software engineering tasks.

## Features

- **Commander Agent**: Lead planner and orchestrator that never writes code
- **Council System**: 1-4 heterogeneous Oracles (Claude, GPT, Gemini, DeepSeek) provide diverse perspectives
- **Operator Agents**: Task executors using Sonnet 4 for implementation
- **Validator Gate**: QA checkpoint before task completion
- **Mission State**: Persistent state that survives context compaction (`.delta9/mission.json`)
- **Memory Blocks**: Cross-session learning with scoped memory

## Installation

```bash
npm install delta9
```

Add to your OpenCode configuration:

```json
{
  "plugins": ["delta9"]
}
```

## Quick Start

```bash
# Start a mission
> Let's build a user authentication system

# Delta9 automatically:
# 1. Commander analyzes the request
# 2. Council provides strategic recommendations
# 3. Operators execute tasks
# 4. Validator verifies completion
```

## Architecture

```
                    ┌─────────────┐
                    │  Commander  │  Strategic Planning
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        ┌─────────┐  ┌─────────┐  ┌─────────┐
        │ Oracle  │  │ Oracle  │  │ Oracle  │  Council
        │ (Claude)│  │ (GPT)   │  │ (Gemini)│
        └────┬────┘  └────┬────┘  └────┬────┘
              └────────────┼────────────┘
                           ▼
                    ┌─────────────┐
                    │  Operators  │  Task Execution
                    └──────┬──────┘
                           ▼
                    ┌─────────────┐
                    │  Validator  │  Quality Gate
                    └─────────────┘
```

## Council Modes

| Mode | When Used | Oracles |
|------|-----------|---------|
| `NONE` | Simple tasks ("fix typo") | Commander only |
| `QUICK` | Moderate tasks ("add page") | 1 Oracle |
| `STANDARD` | Complex tasks ("new feature") | All configured |
| `XHIGH` | Critical ("refactor auth") | All + recon access |

## Configuration

Create `.delta9/config.json`:

```json
{
  "council": {
    "defaultMode": "STANDARD",
    "oracles": ["claude", "gpt", "gemini"],
    "timeout": 30000
  },
  "operators": {
    "maxConcurrent": 3,
    "defaultModel": "sonnet-4"
  }
}
```

## Documentation

| Document | Description |
|----------|-------------|
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Complete usage guide |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | Configuration reference |
| [docs/EXAMPLES.md](docs/EXAMPLES.md) | Example missions and workflows |
| [docs/spec.md](docs/spec.md) | Full specification (SOURCE OF TRUTH) |
| [docs/README.md](docs/README.md) | Documentation navigation hub |
| [CLAUDE.md](CLAUDE.md) | AI assistant instructions |

## Development

```bash
# Install dependencies
npm install

# Build
npm run build

# Run tests
npm run test

# Type check
npm run typecheck

# Lint
npm run lint
```

## Project Status

- **Phase 1**: Foundation - Complete
- **Phase 2**: Council System - Complete
- **Phase 3**: Intelligence Layer - Complete
- **Phase 4**: Robustness - Complete
- **Phase 5**: Polish & Support - Complete
- **Phase 6**: Launch - In Progress

**Overall Progress:** 94% complete (68/72 tasks)

See [docs/BACKLOG.md](docs/BACKLOG.md) for detailed progress tracking.

## License

MIT
