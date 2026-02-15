# Gemini CLI Root Configs Analysis

> Comprehensive analysis of Gemini CLI's root-level files and configurations for AVA reference.

---

## Project Overview

**Name:** `@google/gemini-cli`
**Version:** `0.28.0-nightly.20260128.adc8e11bb`
**License:** Apache 2.0
**Repository:** https://github.com/google-gemini/gemini-cli
**Package:** https://www.npmjs.com/package/@google/gemini-cli

### What Is Gemini CLI?

Gemini CLI is an open-source AI agent that brings Google's Gemini models directly into the terminal. It provides:

- **Free tier**: 60 requests/min and 1,000 requests/day with personal Google account
- **Gemini 3 models**: Improved reasoning and 1M token context window
- **Built-in tools**: Google Search grounding, file operations, shell commands, web fetching
- **MCP support**: Model Context Protocol for custom integrations
- **Terminal-first design**: For developers who live in the command line

### Key Value Propositions

1. **Free access** to powerful Gemini models
2. **1M token context window** (extremely large for code understanding)
3. **Google Search grounding** for real-time information
4. **Open source** (Apache 2.0)
5. **Multiple auth options**: OAuth login, API Key, or Vertex AI

---

## Architecture Decisions

### Monorepo Structure (npm workspaces)

```
gemini-cli/
├── packages/
│   ├── cli/              # User-facing terminal UI (React/Ink)
│   ├── core/             # Backend logic, API orchestration, tools
│   ├── a2a-server/       # Experimental Agent-to-Agent server
│   ├── test-utils/       # Testing utilities
│   └── vscode-ide-companion/  # VS Code extension
├── docs/                 # Documentation
├── evals/                # Evaluation suite
├── integration-tests/    # E2E tests
├── schemas/              # JSON schemas
├── scripts/              # Build and development scripts
└── third_party/          # Third-party dependencies
```

### Core Technologies

| Technology | Purpose |
|------------|---------|
| **Node.js** | Runtime (>=20.0.0, recommended ~20.19.0 for dev) |
| **TypeScript** | Primary language with strict configuration |
| **React + Ink** | CLI UI framework (React for terminal) |
| **esbuild** | Bundling |
| **Vitest** | Testing framework |
| **ESLint + Prettier** | Linting and formatting |
| **Husky** | Git hooks |

### Package Separation

- **`@google/gemini-cli`** (packages/cli): Frontend terminal UI, input processing, display rendering
- **`@google/gemini-cli-core`** (packages/core): Backend logic, Gemini API orchestration, prompt construction, tool execution
- **`@google/gemini-cli-a2a-server`** (packages/a2a-server): Experimental A2A server

### Key Architectural Patterns

1. **React for CLI**: Uses [Ink](https://github.com/vadimdemedes/ink) - React renderer for the terminal
2. **Strict environment isolation**: Helpers for `homedir()` and `tmpdir()` instead of direct `node:os` usage
3. **ESM modules**: Full ES modules (`"type": "module"`)
4. **Composite TypeScript builds**: Incremental compilation with project references

---

## Build System and Tooling

### Node.js Configuration

```
.nvmrc: 20
engines: "node": ">=20.0.0"
Development recommendation: ~20.19.0
```

### npm Scripts

| Script | Purpose |
|--------|---------|
| `npm run start` | Run CLI in development mode |
| `npm run debug` | Run with Node.js inspector |
| `npm run build` | Build all packages |
| `npm run build:all` | Build packages + sandbox + VS Code extension |
| `npm run bundle` | Create production bundle with esbuild |
| `npm run test` | Run unit tests (Vitest) |
| `npm run test:e2e` | Run integration tests |
| `npm run preflight` | Full validation (clean, install, format, build, lint, typecheck, test) |
| `npm run lint` | ESLint |
| `npm run format` | Prettier |
| `npm run typecheck` | TypeScript compilation check |

### esbuild Configuration

**Key Features:**
- Platform: Node.js
- Format: ESM
- WASM support via `esbuild-plugin-wasm`
- External native modules: `node-pty`, `keytar`
- Banner injection for ESM compatibility (`createRequire`, `__dirname`, `__filename`)
- Parallel builds for CLI and A2A server

**External Dependencies (not bundled):**
```javascript
const external = [
  '@lydell/node-pty',
  'node-pty',
  '@lydell/node-pty-darwin-arm64',
  '@lydell/node-pty-darwin-x64',
  '@lydell/node-pty-linux-x64',
  '@lydell/node-pty-win32-arm64',
  '@lydell/node-pty-win32-x64',
  'keytar',
];
```

### TypeScript Configuration

**Strict settings enabled:**
- `strict: true`
- `noImplicitAny: true`
- `noImplicitReturns: true`
- `strictNullChecks: true`
- `strictPropertyInitialization: true`
- `noUnusedLocals: true`
- `noPropertyAccessFromIndexSignature: true`
- `verbatimModuleSyntax: true` (enforces explicit type imports)

**Module Settings:**
- Target: `es2022`
- Module: `NodeNext`
- ModuleResolution: `nodenext`
- Lib: `ES2023`
- JSX: `react-jsx`

**Build Optimization:**
- `composite: true` (project references)
- `incremental: true` (faster rebuilds)

### ESLint Configuration

**Extensive linting setup with:**
- TypeScript ESLint
- React & React Hooks plugins
- Import plugin (no default exports, enforce node: protocol)
- Vitest plugin for test files
- Header enforcement (Apache 2.0 license headers)
- Prettier integration

**Notable Rules:**
- `@typescript-eslint/no-explicit-any: 'error'`
- `@typescript-eslint/no-floating-promises: 'error'`
- `@typescript-eslint/await-thenable: 'error'`
- `@typescript-eslint/return-await: ['error', 'in-try-catch']`
- `import/no-default-export: 'warn'` (prefer named exports)
- `no-console: 'error'` (use debugLogger instead)
- `no-restricted-imports` for `homedir()` and `tmpdir()`

**Self-import Prevention:**
```javascript
// packages/core cannot import from '@google/gemini-cli-core'
// packages/cli cannot import from '@google/gemini-cli'
// Forces relative imports within packages
```

### Prettier Configuration

```json
{
  "semi": true,
  "trailingComma": "all",
  "singleQuote": true,
  "printWidth": 80,
  "tabWidth": 2
}
```

### Git Hooks (Husky + lint-staged)

**Pre-commit hook:**
- Runs `npm run pre-commit`
- Formats and lints staged files
- Fails commit if checks fail

**lint-staged configuration:**
```json
{
  "*.{js,jsx,ts,tsx}": ["prettier --write", "eslint --fix --max-warnings 0"],
  "*.{json,md}": ["prettier --write"]
}
```

---

## Key Features (from Documentation)

### Built-in Tools

1. **File System Operations**: Read, write, edit files
2. **Shell Commands**: Execute bash commands
3. **Web Fetch**: Fetch and process web content
4. **Google Search Grounding**: Real-time search integration

### MCP Server Integration

Configure MCP servers in `~/.gemini/settings.json`:
```
> @github List my open pull requests
> @slack Send a summary of today's commits to #dev channel
> @database Run a query to find inactive users
```

### GEMINI.md Context Files

Custom context files for project-specific behavior (similar to CLAUDE.md).

### Conversation Checkpointing

Save and resume complex sessions.

### Token Caching

Optimize token usage for large codebases.

### GitHub Integration

- **PR Reviews**: Automated code review
- **Issue Triage**: Automated labeling
- **On-demand Assistance**: Mention `@gemini-cli` in issues/PRs

### Sandboxing

**macOS Seatbelt:**
- `permissive-open` profile (default): restricts writes to project folder
- `restrictive-closed` profile: declines all operations by default

**Container-based (all platforms):**
- Docker or Podman support
- Custom sandbox via `.gemini/sandbox.Dockerfile`

### Skills System

Skills are markdown files with frontmatter that guide agent behavior:

```yaml
---
name: code-reviewer
description: Use this skill to review code...
---

# Code Reviewer

This skill guides the agent...
```

**Built-in skills:**
- `code-reviewer`: Code review workflow
- `pr-creator`: Pull request creation
- `docs-writer`: Documentation writing

### Custom Commands (TOML)

Commands are defined in `.gemini/commands/*.toml`:

```toml
description="Injects context of all relevant cli files"
prompt = """
The following output contains the complete source code...
"""
```

---

## Roadmap Items

### Focus Areas

| Area | Description |
|------|-------------|
| **Authentication** | API keys, Gemini Code Assist login |
| **Model** | New Gemini models, multi-modality, local execution |
| **User Experience** | CLI usability, performance, documentation |
| **Tooling** | Built-in tools and MCP ecosystem |
| **Core** | Core CLI functionality |
| **Extensibility** | GitHub integration, other surfaces |
| **Background Agents** | Long-running autonomous tasks |
| **Security and Privacy** | Security improvements |

### Issue Hierarchy

Workstream => Epics => Features => Tasks/Bugs

### Guiding Principles

1. **Power & Simplicity**: State-of-the-art models with intuitive CLI
2. **Extensibility**: Adaptable agent for various use cases
3. **Intelligent**: Ranked among best agentic tools (SWE Bench, Terminal Bench)
4. **Free and Open Source**: No cost barrier, quick PR merges

---

## CI/CD Configuration

### GitHub Actions Workflows

**Main CI (`ci.yml`):**
- Triggered on: push to main/release, PRs, merge queue
- Jobs:
  - **Lint**: ESLint, actionlint, shellcheck, yamllint, Prettier
  - **Link Checker**: Lychee for broken links
  - **Test (Linux)**: Node 20.x, 22.x, 24.x with sharded tests
  - **Test (Mac)**: Same Node versions, sharded
  - **Test (Windows)**: Node 20.x only
  - **CodeQL**: Security analysis
  - **Bundle Size**: Check for bundle size changes

**Test Matrix:**
```yaml
node-version: ['20.x', '22.x', '24.x']
shard: ['cli', 'others']  # Split tests for parallelism
```

**Custom runners:**
- `gemini-cli-ubuntu-16-core`
- `gemini-cli-windows-16-core`
- `macos-latest`

### Dependabot

- Weekly updates (Monday)
- Groups minor/patch updates
- Separate configs for npm and GitHub Actions

### Release Cadence

| Tag | Frequency | Description |
|-----|-----------|-------------|
| `nightly` | Daily (UTC 0000) | All changes from main |
| `preview` | Weekly (Tuesday UTC 2359) | Pre-release testing |
| `latest` | Weekly (Tuesday UTC 2000) | Stable release |

---

## Testing Strategy

### Unit Tests

```bash
npm run test              # All workspaces
npm run test:ci           # CI-specific with coverage
npm test -w <pkg> -- <path>  # Single file
```

### Integration Tests

```bash
npm run test:e2e                    # No sandbox
npm run test:integration:sandbox:docker  # Docker sandbox
npm run test:integration:sandbox:podman  # Podman sandbox
```

### Evaluation Suite

```bash
npm run test:always_passing_evals  # Quick evals
npm run test:all_evals             # Full eval suite (RUN_EVALS=1)
```

### Environment Variable Testing

```typescript
// Recommended pattern
vi.stubEnv('NAME', 'value');  // in beforeEach
vi.unstubAllEnvs();           // in afterEach
```

---

## Optional Dependencies

For PTY (pseudo-terminal) support across platforms:

```json
{
  "@lydell/node-pty": "1.1.0",
  "@lydell/node-pty-darwin-arm64": "1.1.0",
  "@lydell/node-pty-darwin-x64": "1.1.0",
  "@lydell/node-pty-linux-x64": "1.1.0",
  "@lydell/node-pty-win32-arm64": "1.1.0",
  "@lydell/node-pty-win32-x64": "1.1.0",
  "keytar": "^7.9.0",
  "node-pty": "^1.0.0"
}
```

Uses @lydell/node-pty fork for better compatibility.

---

## Notable npm Overrides

```json
{
  "overrides": {
    "ink": "npm:@jrichman/ink@6.4.8",  // Forked Ink version
    "wrap-ansi": "9.0.2",
    "cliui": {
      "wrap-ansi": "7.0.0"
    }
  }
}
```

Uses a forked version of Ink (`@jrichman/ink`) for custom features or fixes.

---

## Key Takeaways for AVA

### What to Adopt

1. **Monorepo structure**: Clean separation of CLI/core/extensions
2. **Strict TypeScript**: All strict flags enabled
3. **ESM-first**: Full ES modules with proper Node.js resolution
4. **esbuild bundling**: Fast builds with proper handling of native modules
5. **Comprehensive linting**: ESLint + Prettier + type-checking
6. **Sharded testing**: Parallel test execution for faster CI
7. **Skills/Commands system**: Extensible agent customization
8. **Sandboxing options**: Security-first execution environments
9. **Environment isolation**: Wrappers for `homedir()`/`tmpdir()`
10. **License headers**: Enforced via ESLint plugin

### Differences from AVA

| Aspect | Gemini CLI | AVA |
|--------|------------|--------|
| UI Framework | React (Ink) for CLI | SolidJS for Tauri |
| Platform | Node.js CLI only | Tauri desktop + CLI |
| Bundler | esbuild | Vite |
| Package Manager | npm workspaces | npm workspaces |
| Linting | ESLint + Prettier | Biome + Oxlint + ESLint |

### Reference Code Value

- **PTY implementation**: Cross-platform terminal handling
- **MCP transport patterns**: See `packages/core/` for MCP client
- **Tool execution**: How they handle shell, file, and web tools
- **Context management**: Token caching and context window handling
- **Sandboxing**: macOS Seatbelt and container-based approaches

---

## Files Analyzed

| File | Purpose |
|------|---------|
| `README.md` | Project overview, installation, features |
| `GEMINI.md` | Project context for AI assistants |
| `CONTRIBUTING.md` | Contribution guidelines, development setup |
| `ROADMAP.md` | Development priorities and focus areas |
| `package.json` | Dependencies, scripts, workspaces |
| `tsconfig.json` | TypeScript configuration |
| `eslint.config.js` | Linting rules |
| `esbuild.config.js` | Bundle configuration |
| `.prettierrc.json` | Code formatting |
| `.nvmrc` | Node.js version |
| `.editorconfig` | Editor settings |
| `.gitattributes` | Git line ending settings |
| `.gitignore` | Ignored files |
| `.husky/pre-commit` | Git hooks |
| `.github/workflows/ci.yml` | CI configuration |
| `.github/dependabot.yml` | Dependency updates |
| `Dockerfile` | Sandbox container |
| `Makefile` | Build shortcuts |
| `.gemini/config.yaml` | Gemini bot configuration |
| `.gemini/skills/*.md` | Agent skills |
| `.gemini/commands/*.toml` | Custom commands |

---

*Last Updated: 2026-02-04*
