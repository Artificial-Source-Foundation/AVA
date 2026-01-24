# Delta9 Documentation Creation Plan

> Creating comprehensive, AI-optimized documentation for the Delta9 OpenCode plugin

---

## Executive Summary

This plan outlines the creation of documentation optimized for AI coding agents. The documentation will enable any AI (Claude Code, OpenAI Codex, Google Jules, Cursor) to understand OpenCode's plugin system and build Delta9 effectively within minutes.

---

## Phase 1: AI Documentation Research Summary

### Key Standards Researched

| Standard | Purpose | Key Insights |
|----------|---------|--------------|
| **llms.txt** | AI-friendly website content index | H1 title + blockquote summary + H2 sections with links. Placed at root. |
| **AGENTS.md** | Agent-specific instructions | No schema required. ~150 lines max. Cover: commands, testing, code style, boundaries. |
| **CLAUDE.md** | Claude Code project context | Concise, hierarchical, use "IMPORTANT" for emphasis, iterate like a prompt. |
| **.cursorrules** | Cursor AI rules | Deprecated -> use `.cursor/rules/*.mdc`. Under 500 lines. |

### Best Practices Identified

1. **Front-load critical info** - Most important content first
2. **Be scannable** - Headers, bullets, code blocks
3. **Link, don't duplicate** - Point to detailed docs
4. **Include examples** - Code snippets for patterns
5. **Stay specific** - Exact file paths, exact commands
6. **Three-tier boundaries** - Always do / Ask first / Never do
7. **Put commands early** - Build, test, lint commands prominently
8. **<=150 lines per AGENTS.md** - Keep focused, avoid burying signal

---

## Phase 2: OpenCode Plugin Documentation Summary

### Core Concepts Gathered

| Topic | Key Details |
|-------|-------------|
| **Plugin Loading** | `.opencode/plugins/` (local), `~/.config/opencode/plugins/` (global), npm packages in `opencode.json` |
| **Plugin Entry** | `export const MyPlugin: Plugin = async ({ project, client, $, directory, worktree }) => { ... }` |
| **Event Hooks** | `session.created`, `session.compacted`, `tool.execute.before`, `tool.execute.after`, etc. |
| **Custom Tools** | `tool({ description, args: { ... }, async execute(args, ctx) { ... } })` with Zod schemas |
| **Agents** | JSON in `opencode.json` or markdown in `.opencode/agents/` with YAML frontmatter |
| **Commands** | Markdown in `.opencode/commands/`, `$ARGUMENTS` for params, `!`cmd`` for shell injection |
| **MCP Servers** | Local (`type: "local"`) or remote (`type: "remote"`) with OAuth support |
| **SDK** | `@opencode-ai/sdk` for programmatic control, `client.app.log()` for logging |

### Oh-My-OpenCode Patterns Observed

- **Multi-agent orchestration**: Sisyphus (main) + specialists (Prometheus, Oracle, Librarian)
- **Context injection**: Auto-embed AGENTS.md, README.md into agent prompts
- **Background parallelization**: Multiple agents work concurrently
- **Curated model selection**: Different models for different task types
- **Hook system**: 25+ configurable hooks
- **Magic word activation**: "ultrawork" triggers full orchestration

---

## Phase 3: Documentation Structure Plan

### Root Files

```
Delta9/
├── CLAUDE.md              # Primary AI entry point
├── llms.txt               # AI-readable index (llmstxt.org standard)
├── llms-full.txt          # Complete content for RAG systems
├── AGENTS.md              # Universal agent instructions (agents.md standard)
├── .cursorrules           # Cursor AI rules (legacy compat)
└── .claude/
    └── AGENTS.md          # Claude Code specific (if needed)
```

### Documentation Folder

```
docs/
├── AI_DOCS_RESEARCH.md            # Phase 1 findings (for reference)
│
├── OPENCODE_REFERENCE/            # OpenCode plugin system docs
│   ├── 00_OVERVIEW.md             # What is OpenCode, quick start
│   ├── 01_PLUGIN_SYSTEM.md        # Plugin loading, entry points, context
│   ├── 02_AGENTS.md               # Agent definitions (JSON & markdown)
│   ├── 03_TOOLS.md                # Built-in tools, custom tools
│   ├── 04_HOOKS.md                # Event hooks available
│   ├── 05_MCP.md                  # MCP server integration
│   ├── 06_CONFIG.md               # Configuration options
│   ├── 07_SDK.md                  # SDK reference
│   └── 08_MODELS.md               # Model configuration
│
├── PATTERNS/                      # Best practices from ecosystem
│   ├── OH_MY_OPENCODE.md          # Patterns from the gold standard
│   ├── PLUGIN_EXAMPLES.md         # Other plugin patterns
│   └── BEST_PRACTICES.md          # Collected best practices
│
├── ARCHITECTURE.md                # Delta9 system design
├── AGENTS.md                      # Delta9 agent definitions
├── API.md                         # Internal API reference
└── DEVELOPMENT.md                 # Developer workflow
```

---

## Phase 4: File-by-File Content Plan

### 4.1 CLAUDE.md (Root - ~200 lines)

```markdown
# Delta9 - OpenCode Plugin

> Strategic AI Coordination for Mission-Critical Development

## Quick Context
Delta9 implements Commander + Council + Operators architecture...

## Commands (Run These)
- `npm run build` - Build TypeScript
- `npm run test` - Run tests
- `npm run lint` - Check code style

## Project Structure
[File tree with one-line descriptions]

## Key Concepts
- Commander: Lead planner, never writes code
- Council: 4 heterogeneous Oracles (Claude, GPT, Gemini, DeepSeek)
- Operators: Task executors (Sonnet 4)
- Validators: QA verification (Haiku)
- Mission State: External persistence in .delta9/

## Documentation Index
- `docs/ARCHITECTURE.md` - System design deep-dive
- `docs/OPENCODE_REFERENCE/` - OpenCode plugin docs
- `docs/PATTERNS/` - Best practices

## Critical Files (Read These First)
1. `docs/spec.md` - Full specification (source of truth)
2. `src/index.ts` - Plugin entry point
3. `src/mission/state.ts` - Mission state manager
4. `src/types/` - Type definitions

## Current Status
- Phase: Specification complete, Phase 1 implementation starting
- Built: Nothing yet (scaffold only)
- Next: Plugin scaffold, config system, Commander agent

## Coding Conventions
- TypeScript strict mode
- Zod for all runtime validation
- Functional patterns preferred
- camelCase functions, PascalCase types
- kebab-case files

## Architecture Decisions
1. External mission.json for compaction survival
2. Commander never touches code (protected context)
3. Validator gate before any task completion
4. Graceful degradation (works with any model combo)

## Common Tasks

### Adding a new agent
1. Create `src/agents/[category]/[name].ts`
2. Define agent with OpenCode patterns
3. Export from `src/agents/index.ts`
4. Add to Commander's dispatch logic

### Modifying mission state
1. Update `src/mission/schema.ts` (Zod)
2. Update `src/types/mission.ts`
3. Update `src/mission/state.ts` methods

## Testing
`npm run test` - Vitest
Debug with `client.app.log()` during development

## Reference
- OpenCode docs: https://opencode.ai/docs/
- oh-my-opencode: https://github.com/code-yeongyu/oh-my-opencode
- Spec: docs/spec.md
```

### 4.2 llms.txt (Root - ~50 lines)

```markdown
# Delta9

> An OpenCode plugin implementing Commander + Council + Operators architecture for strategic AI coordination in mission-critical development.

## Documentation

- [CLAUDE.md](CLAUDE.md): Project overview and AI instructions
- [AGENTS.md](AGENTS.md): Universal agent instructions
- [Specification](docs/spec.md): Full system specification

## OpenCode Reference

- [Plugin System](docs/OPENCODE_REFERENCE/01_PLUGIN_SYSTEM.md): How plugins work
- [Agents](docs/OPENCODE_REFERENCE/02_AGENTS.md): Agent definitions
- [Tools](docs/OPENCODE_REFERENCE/03_TOOLS.md): Built-in and custom tools
- [Hooks](docs/OPENCODE_REFERENCE/04_HOOKS.md): Event hooks
- [Configuration](docs/OPENCODE_REFERENCE/06_CONFIG.md): Config options

## Architecture

- [Architecture](docs/ARCHITECTURE.md): System design
- [Delta9 Agents](docs/AGENTS.md): Agent roster details
- [API Reference](docs/API.md): Internal API

## Optional

- [Development](docs/DEVELOPMENT.md): Developer workflow
- [Patterns](docs/PATTERNS/): Best practices collection
```

### 4.3 AGENTS.md (Root - ~100 lines)

```markdown
# Delta9 Agent Instructions

## Commands

npm run build      # Build TypeScript
npm run test       # Run Vitest tests
npm run lint       # ESLint check
npm run typecheck  # TypeScript check

## Project Structure

src/
├── index.ts           # Plugin entry point
├── agents/            # Agent definitions
├── mission/           # State management
├── council/           # Council orchestration
├── hooks/             # OpenCode event hooks
├── tools/             # Custom tools
└── types/             # Type definitions

## Code Style

- TypeScript strict mode required
- Use Zod for runtime validation
- Export types from `src/types/`
- One component per file when possible
- kebab-case for filenames

## Testing

- Framework: Vitest
- Run: `npm run test`
- New features require tests

## Boundaries

### Always Do
- Read spec.md before making changes
- Validate data with Zod schemas
- Use OpenCode plugin patterns
- Test changes before committing

### Ask First
- Modifying mission.json schema
- Adding new agent types
- Changing Commander dispatch logic

### Never Do
- Use `any` type
- Mutate mission state directly
- Skip the Validator gate
- Hardcode model names

## Key Files

1. `docs/spec.md` - Source of truth
2. `src/mission/state.ts` - Mission state manager
3. `src/agents/commander.ts` - Lead orchestrator
```

### 4.4 docs/OPENCODE_REFERENCE/01_PLUGIN_SYSTEM.md

Content extracted from research:
- Plugin loading locations
- Entry point signature
- Context parameters (project, client, $, directory, worktree)
- TypeScript types
- Dependencies
- Example skeleton

### 4.5 docs/OPENCODE_REFERENCE/02_AGENTS.md

Content from research:
- Primary vs subagent modes
- JSON configuration
- Markdown format with frontmatter
- Temperature, model, permissions
- Tool access control
- Task permissions

### 4.6 docs/OPENCODE_REFERENCE/03_TOOLS.md

Content:
- 14 built-in tools (read, write, edit, bash, glob, grep, etc.)
- Custom tool creation with `tool()` helper
- Zod schema for arguments
- Context parameters (agent, sessionID, messageID)
- Tool locations

### 4.7 docs/OPENCODE_REFERENCE/04_HOOKS.md

Content:
- Available event hooks
- Hook handler signatures
- Common patterns
- Note: 404 on official docs, use oh-my-opencode as reference

### 4.8 docs/ARCHITECTURE.md

Extract and expand from spec.md:
- ASCII flow diagrams
- Agent interaction patterns
- State management approach
- Phase breakdown

### 4.9 docs/AGENTS.md

Document each Delta9 agent:
- Commander (role, model, when invoked, example prompts)
- 4 Oracles
- Operator, Validator, Patcher
- 7 Support agents

### 4.10 docs/PATTERNS/OH_MY_OPENCODE.md

Document patterns observed:
- Multi-agent orchestration
- Context injection
- Background parallelization
- Hook system
- Configuration patterns

---

## Phase 5: Implementation Order

### Step 1: Create Folder Structure
```bash
mkdir -p docs/OPENCODE_REFERENCE
mkdir -p docs/PATTERNS
mkdir -p .claude
```

### Step 2: Create Root Files (Priority Order)
1. `CLAUDE.md` - Primary AI entry point
2. `AGENTS.md` - Universal agent instructions
3. `llms.txt` - AI index
4. `.cursorrules` - Cursor compatibility

### Step 3: Create OpenCode Reference Docs
1. `01_PLUGIN_SYSTEM.md` - Critical
2. `02_AGENTS.md`
3. `03_TOOLS.md`
4. `04_HOOKS.md`
5. `00_OVERVIEW.md`
6. `05_MCP.md`
7. `06_CONFIG.md`
8. `07_SDK.md`
9. `08_MODELS.md`

### Step 4: Create Pattern Docs
1. `OH_MY_OPENCODE.md`
2. `BEST_PRACTICES.md`
3. `PLUGIN_EXAMPLES.md`

### Step 5: Create Delta9-Specific Docs
1. `ARCHITECTURE.md`
2. `AGENTS.md` (Delta9 agents)
3. `API.md`
4. `DEVELOPMENT.md`

### Step 6: Create Supporting Files
1. `AI_DOCS_RESEARCH.md` - Research findings
2. `llms-full.txt` - Complete content
3. `.claude/AGENTS.md` - Claude-specific

---

## Quality Checklist

Each documentation file must:

- [ ] Have clear purpose stated at top
- [ ] Be self-contained (readable independently)
- [ ] Include code examples where relevant
- [ ] Use consistent formatting (headers, code blocks, lists)
- [ ] Link to sources
- [ ] Be accurate (verified against OpenCode behavior)
- [ ] Be concise (no fluff, respect token limits)
- [ ] Follow the relevant standard (llms.txt, AGENTS.md, CLAUDE.md specs)

---

## Estimated Output

| Category | Files | Total Lines |
|----------|-------|-------------|
| Root AI files | 4 | ~450 |
| OpenCode Reference | 9 | ~1,500 |
| Patterns | 3 | ~600 |
| Delta9 Specific | 4 | ~800 |
| Research | 1 | ~300 |
| **Total** | **21 files** | **~3,650 lines** |

---

## Sources Used

### AI Documentation Standards
- [llms.txt Specification](https://llmstxt.org/)
- [AGENTS.md Standard](https://agents.md/)
- [GitHub: How to write a great agents.md](https://github.blog/ai-and-ml/github-copilot/how-to-write-a-great-agents-md-lessons-from-over-2500-repositories/)
- [Claude Code Best Practices](https://www.anthropic.com/engineering/claude-code-best-practices)
- [awesome-cursorrules](https://github.com/PatrickJS/awesome-cursorrules)

### OpenCode Documentation
- [OpenCode Overview](https://opencode.ai/docs/)
- [OpenCode Plugin System](https://opencode.ai/docs/plugins/)
- [OpenCode Agents](https://opencode.ai/docs/agents/)
- [OpenCode Tools](https://opencode.ai/docs/tools/)
- [OpenCode Custom Tools](https://opencode.ai/docs/custom-tools/)
- [OpenCode Configuration](https://opencode.ai/docs/config/)
- [OpenCode SDK](https://opencode.ai/docs/sdk/)
- [OpenCode Commands](https://opencode.ai/docs/commands/)
- [OpenCode MCP Servers](https://opencode.ai/docs/mcp-servers/)

### Reference Implementations
- [oh-my-opencode](https://github.com/code-yeongyu/oh-my-opencode)

---

## Ready for Implementation

This plan provides a complete roadmap for creating AI-optimized documentation. Upon approval, I will create all 21 files in the order specified above.

**Next Step**: Exit plan mode and begin implementation with Step 1 (folder structure) and Step 2 (root files).
