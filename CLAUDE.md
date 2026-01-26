# Delta9 - OpenCode Plugin

> Strategic AI Coordination for Mission-Critical Development

Delta9 is an OpenCode plugin implementing a hierarchical **Commander + Council + Operators** architecture that separates planning from execution, maintains mission state across context compactions, and verifies all work against acceptance criteria.

---

## Commands

```bash
npm run build      # Build TypeScript
npm run test       # Run Vitest tests
npm run lint       # ESLint check
npm run typecheck  # TypeScript strict check
```

---

## Documentation (Start Here)

| Priority | Path | Description |
|----------|------|-------------|
| **1** | `docs/spec.md` | Full specification (SOURCE OF TRUTH) |
| **2** | `docs/README.md` | Documentation navigation hub |
| **3** | `docs/plugin-guide/` | How to build OpenCode plugins (14 files) |
| **4** | `docs/opencode/` | OpenCode platform reference |
| **5** | `docs/delta9/` | Delta9 architecture & API |
| **6** | `docs/patterns/` | Best practices |

---

## Project Structure

```
delta9/
├── src/
│   ├── index.ts              # Plugin entry point
│   ├── agents/               # Agent definitions (19 agents)
│   │   ├── commander.ts      # Lead planner & orchestrator
│   │   ├── operator.ts       # Task executor
│   │   ├── validator.ts      # QA verification
│   │   ├── council/          # 4 Oracle agents (Cipher, Vector, Prism, Apex)
│   │   └── support/          # 8 Delta Team agents (RECON, SIGINT, etc.)
│   ├── mission/              # Mission state management
│   ├── orchestration/        # Council orchestration
│   ├── routing/              # Task routing
│   ├── hooks/                # OpenCode event hooks
│   ├── tools/                # Custom tools (56 tools)
│   ├── lib/                  # Utilities
│   └── types/                # TypeScript types
├── docs/
│   ├── README.md             # Navigation hub
│   ├── spec.md               # Full specification
│   ├── delta9/               # Delta9-specific docs
│   ├── opencode/             # OpenCode reference
│   ├── plugin-guide/         # Plugin development (14 files)
│   ├── patterns/             # Best practices
│   └── reference-code/       # 41 plugin examples (gitignored)
└── package.json
```

---

## Key Concepts

| Concept | Description |
|---------|-------------|
| **Commander** | Lead planner. NEVER writes code. |
| **Council** | 4 Oracles: Cipher, Vector, Prism, Apex |
| **Delta Team** | 8 support agents: RECON, SIGINT, TACCOM, SURGEON, SENTINEL, SCRIBE, FACADE, SPECTRE |
| **Operators** | Task executors (Sonnet 4) |
| **Validator** | QA gate (Haiku) |
| **Mission State** | Persisted in `.delta9/mission.json` |

---

## Quick Pattern Lookup

| Need | Look At |
|------|---------|
| Plugin entry | `docs/plugin-guide/01-architecture.md` |
| Configuration | `docs/plugin-guide/02-configuration.md` |
| Hook system | `docs/plugin-guide/03-hooks.md` |
| Tool definition | `docs/plugin-guide/04-tools.md` |
| Memory | `docs/plugin-guide/05-memory.md` |
| Skills | `docs/plugin-guide/06-skills.md` |
| Safety | `docs/plugin-guide/07-safety.md` |
| Background tasks | `docs/plugin-guide/08-background-tasks.md` |
| Terminal | `docs/plugin-guide/09-terminal.md` |
| DX | `docs/plugin-guide/10-dx.md` |
| Patterns | `docs/plugin-guide/11-patterns.md` |
| Templates | `docs/plugin-guide/12-templates.md` |
| Setup | `docs/plugin-guide/13-setup.md` |
| Reference | `docs/plugin-guide/14-reference.md` |

---

## Coding Conventions

- **TypeScript**: Strict mode, no `any`
- **Validation**: Zod for all runtime data
- **Naming**: camelCase functions, PascalCase types, kebab-case files
- **Files**: Max 300 lines, one component per file

---

## Boundaries

### Always Do

- Read `docs/spec.md` before architectural changes
- Validate runtime data with Zod
- Persist state via `MissionState` class
- Test before committing

### Never Do

- Use `any` type
- Mutate mission state directly
- Skip Validator gate
- Let Commander write code

---

## Current Status

- **Phase**: Phase 2 Council System (in progress)
- **Tests**: 651 unit tests passing
- **Built**: Plugin scaffold, config system, Commander, Operators, Validator, 56 tools, 19 agents (including Delta Team + Council)
- **Next**: Phase 3 - Mission Orchestration

See `docs/BACKLOG.md` for detailed task tracking.
