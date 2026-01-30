# Estela

> Multi-Agent AI Coding Assistant

---

## Memory Bank

**IMPORTANT: Read these files at the start of every session:**

| File | Purpose | Update |
|------|---------|--------|
| [`activeContext.md`](docs/memory-bank/activeContext.md) | Current tasks & focus | Every session |
| [`progress.md`](docs/memory-bank/progress.md) | What's been done | Every session |
| [`techContext.md`](docs/memory-bank/techContext.md) | Architecture & patterns | When arch changes |
| [`projectbrief.md`](docs/memory-bank/projectbrief.md) | What is Estela | Rarely |

**Workflow:**
1. Start → Read `activeContext.md`
2. Work → Update as focus changes
3. End → Update `progress.md`
4. Context full? → "Update memory bank" then `/clear`

---

## Commands

```bash
# Development
npm run tauri dev      # Run app
npm run lint           # Oxlint + ESLint
npm run lint:fix       # Auto-fix lint issues
npm run format         # Biome format
npm run format:check   # Check formatting
npx tsc --noEmit       # Type check

# Testing
npm run test           # Vitest watch
npm run test:run       # Single run
npm run test:coverage  # Coverage report

# Code Quality
npm run knip           # Dead code detection
npm run knip:fix       # Remove dead code
npm run analyze        # Bundle size analysis
```

---

## Tooling Stack

| Tool | Purpose |
|------|---------|
| Biome | Fast formatter + linter (replaces Prettier) |
| Oxlint | Fast linter (50-100x ESLint) |
| ESLint | SolidJS-specific rules |
| Lefthook | Git hooks (pre-commit, commit-msg) |
| commitlint | Conventional commit validation |
| Vitest | Test runner |
| Knip | Dead code finder |
| Renovate | Auto dependency updates |

---

## Planning

| Doc | Purpose |
|-----|---------|
| [`ROADMAP.md`](docs/ROADMAP.md) | High-level epic overview |
| [`development/epics/`](docs/development/epics/) | Detailed sprint planning |
| [`development/completed/`](docs/development/completed/) | Archived done sprints |

---

## Reference Code

**IMPORTANT:** Compare implementations against SOTA projects in `docs/reference-code/`:

| Project | Stars | Key Patterns |
|---------|-------|--------------|
| OpenCode | 70k+ | Tool registry, 2000 line limits, workdir pattern, timeout handling |
| Gemini CLI | 50k+ | ToolBuilder separation, error types, shell execution |

Use these references when implementing new features to ensure we follow best practices.

---

## Code Style

- TypeScript strict, no `any`
- Max 300 lines per file
- kebab-case files, camelCase functions, PascalCase types

---

## Architecture

```
useChat() → resolveAuth() → createClient() → stream()
                ↓
    OAuth → Direct Key → OpenRouter (fallback)
```

See `docs/memory-bank/techContext.md` for details.
