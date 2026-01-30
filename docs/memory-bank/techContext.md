# Tech Context

> Technical decisions and patterns - update when architecture changes

---

## Stack

| Layer | Choice | Why |
|-------|--------|-----|
| Desktop | Tauri 2.0 | Small binary (~3-10MB), Rust backend |
| Frontend | SolidJS | Fine-grained reactivity for streaming |
| Styling | Tailwind v4 | Utility-first, fast iteration |
| Database | SQLite | Local-first, no server needed |
| LLM | Frontend-first | Simpler SSE, faster iteration |

---

## Key Patterns

### LLM Client Architecture

```
resolveAuth(model) → { provider, credentials, useGateway }
       ↓
createClient(provider) → LLMClient
       ↓
client.stream(messages, config, signal) → AsyncGenerator<StreamDelta>
```

### Auth Priority

1. OAuth token (if valid, not expired)
2. Direct API key for provider
3. OpenRouter gateway fallback

### Streaming

- **OpenRouter**: OpenAI-compatible SSE format
- **Anthropic**: Anthropic-specific SSE format
- Both yield `StreamDelta { content, done, usage?, error? }`

---

## File Structure

```
src/
├── types/llm.ts                    # LLM types
├── services/
│   ├── auth/credentials.ts         # Credential management
│   ├── llm/
│   │   ├── client.ts               # LLMClient interface
│   │   └── providers/
│   │       ├── openrouter.ts
│   │       └── anthropic.ts
│   └── database.ts                 # SQLite operations
├── hooks/useChat.ts                # Chat hook
├── stores/session.ts               # Session state
└── components/chat/
    ├── ChatView.tsx
    ├── MessageList.tsx
    ├── MessageInput.tsx
    └── TypingIndicator.tsx
```

---

## Database Schema

```sql
sessions: id, name, created_at, updated_at
messages: id, session_id, role, content, created_at
files: id, session_id, path, operation, diff, created_at
```

---

## Commands

```bash
# Development
npm run tauri dev      # Run app
npm run tauri build    # Production build

# Code Quality
npm run lint           # Oxlint + ESLint
npm run lint:fix       # Auto-fix lint issues
npm run format         # Biome format
npm run format:check   # Check formatting
npx tsc --noEmit       # Type check

# Testing
npm run test           # Vitest watch
npm run test:run       # Single run
npm run test:coverage  # Coverage report

# Analysis
npm run knip           # Dead code detection
npm run knip:fix       # Remove dead code
npm run analyze        # Bundle size analysis
```

---

## Development Tooling

| Tool | Version | Purpose |
|------|---------|---------|
| **Biome** | 2.x | Formatter + linter (7-100x Prettier) |
| **Oxlint** | latest | Fast linter (50-100x ESLint) |
| **ESLint** | 9.x | SolidJS-specific rules via eslint-plugin-solid |
| **Lefthook** | latest | Git hooks (parallel pre-commit) |
| **commitlint** | latest | Conventional Commits enforcement |
| **Vitest** | 3.x | Testing framework (SolidJS-native) |
| **Knip** | 5.x | Dead code detection |
| **Renovate** | - | Automated dependency updates (weekly PRs) |

### Git Hooks (Lefthook)

**pre-commit** (parallel):
1. `biome check --write` on staged files
2. `oxlint` on staged files
3. `tsc --noEmit` type check

**commit-msg**:
- `commitlint` validates conventional commit format

### CI/CD (GitHub Actions)

**ci.yml** (on PR/push to master):
- Lint, typecheck, test, knip, build

**release.yml** (on tag v*):
- Cross-platform Tauri builds via tauri-action

---

## Environment

Dev credentials via `.env.local` (gitignored):
```
VITE_ANTHROPIC_API_KEY=sk-ant-...
VITE_OPENROUTER_API_KEY=sk-or-...
```

Or set via Settings modal in the app.

---

## Decisions Log

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | Frontend-first LLM | Simpler streaming, faster iteration |
| 2 | Multi-provider auth | Flexibility: gateway + direct + OAuth |
| 3 | AsyncGenerator streaming | Clean pattern, works with abort |
| 4 | SolidJS signals | Fine-grained updates during streaming |
| 5 | TypeScript for file ops | Industry standard (OpenCode, Gemini CLI) - see [ADR-001](../architecture/decisions/001-file-operations-typescript.md) |

---

## Reference Code

Cloned open-source AI coding agents in `docs/reference-code/` (gitignored):

| Project | Language | Learn From |
|---------|----------|------------|
| OpenCode | TypeScript | Tool registry, file ops, LSP |
| Aider | Python | Git integration, repo mapping |
| Goose | Rust | MCP protocol, extensibility |
| Plandex | Go | Multi-file planning |
| Gemini CLI | TypeScript | Google's tool patterns |
| OpenHands | Python | Full agent platform |

See [reference-code/README.md](../reference-code/README.md) for clone commands.
