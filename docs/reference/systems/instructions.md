# Project Instructions

AVA discovers instruction files from multiple locations to provide
project-specific context to the LLM. The runtime now uses a hybrid model:

- startup prompt: lean, mostly `AGENTS.md`-based instructions
- on-demand prompt additions: matching `.ava/rules/*.md` after the agent touches
  relevant files

All instruction loading logic lives in `crates/ava-agent/src/instructions.rs`.

## Discovery Order

## Startup Prompt Sources

The initial system-prompt suffix stays intentionally small. Startup loads:

### 1. Global User Instructions

```
~/.ava/AGENTS.md
```

### 2. Ancestor Directory Walk

Starting from the current working directory's parent, walk upward looking for
`AGENTS.md` in each ancestor directory. Stop at the first directory containing
a `.git` folder (repository boundary).

### 3. Project Root AGENTS

```
AGENTS.md
.ava/AGENTS.md
```

### 4. User-Configured Extra Paths

Additional paths from `config.yaml` under the `instructions:` key remain eager
because they are explicit user configuration.

Global and trusted project skill directories also remain available at startup.
Cross-tool root files like `.cursorrules` and `.github/copilot-instructions.md`
are part of the broader discovery surface, but not the lean startup prompt.

## Full Discovery Order

The underlying loader can still discover a broader instruction surface, in this
order (earlier entries appear first, later entries take precedence for
conflicting guidance):

### 1. Global User Instructions

```
~/.ava/AGENTS.md
```

Applies to all projects for this user. Loaded first so project-specific
instructions can override.

### 2. Ancestor Directory Walk

Starting from the current working directory's parent, walk upward looking for
`AGENTS.md` in each ancestor directory. Stop at the first directory containing a
`.git` folder (repository boundary).

Ancestors are loaded in **top-down order** (outermost first) so that
more-specific subdirectory rules take priority over parent rules.

This supports monorepo setups where a top-level `AGENTS.md` provides general
guidance and subdirectory `AGENTS.md` files add specific rules.

### 3. Project Root Files

Checked in this order (all that exist are included):

```
AGENTS.md
.cursorrules
.github/copilot-instructions.md
```

These are the well-known instruction file names defined in
`PROJECT_ROOT_FILES` (`crates/ava-agent/src/instructions.rs:11`).

### 4. Project .ava Directory

```
.ava/AGENTS.md
```

An additional instruction file inside the project's `.ava/` directory.

### 5. Scoped Rules Directory

```
.ava/rules/*.md
```

All `.md` files in this directory are loaded, sorted alphabetically. These
support optional frontmatter for path-scoped rules (see below).

### 6. User-Configured Extra Paths

Additional paths from `config.yaml` under the `instructions:` key. These can
be file paths or glob patterns relative to the project root:

```yaml
instructions:
  - "team/conventions.md"
  - "docs/rules/*.md"
```

Glob patterns are expanded and matched files are loaded in sorted order.

## Deduplication

All loaded files are tracked by canonical path. If the same file is referenced
multiple times (e.g., via symlink or redundant configuration), it appears only
once in the output (`crates/ava-agent/src/instructions.rs:280`).

Empty files (or files containing only whitespace) are silently skipped.

## Cross-Tool Compatibility

AVA reads instruction files from other AI coding tools:

| File | Tool | Notes |
|---|---|---|
| `AGENTS.md` | AVA native | Primary instruction format |
| `.cursorrules` | Cursor | Loaded from project root |
| `.github/copilot-instructions.md` | GitHub Copilot | Standard location |

`CLAUDE.md` is no longer loaded into AVA's runtime system prompt. If you want to
reuse Claude-specific guidance with AVA, move the content into `AGENTS.md` or an
explicit `instructions:` entry.

## Glob-Scoped Rules with Frontmatter

Files in `.ava/rules/` can include YAML frontmatter to restrict when they are
loaded. If `paths:` globs are specified, the rule file is injected only after
the agent touches a matching file path.

Example `.ava/rules/python.md`:

```markdown
---
paths:
  - "**/*.py"
  - "scripts/**"
---
Always use type hints in function signatures.
Prefer dataclasses over plain dicts for structured data.
```

This rule is injected only after the agent reads or edits a matching `.py` file
or path under `scripts/`. The frontmatter is stripped from the output -- only
the body text is injected.

Rules without frontmatter activate on the first touched file in the project.

## Contextual Per-File Instructions

When the agent reads a file, AVA can inject directory-specific instructions
via `contextual_instructions_for_file(file_path, project_root)`
(`crates/ava-agent/src/instructions.rs:253`).

This function walks from the file's parent directory up to the project root,
looking for the nearest `AGENTS.md`. If found, its content is returned for
injection into the tool result context.

The most specific (closest to the file) `AGENTS.md` wins. This enables
per-directory coding conventions:

```
project/
  AGENTS.md              # "Use consistent error handling"
  src/
    api/
      AGENTS.md          # "Use REST conventions, return JSON"
      handler.rs         # Reading this file injects api/AGENTS.md
    db/
      handler.rs         # Reading this file injects project/AGENTS.md
```

## agents.toml Configuration

Sub-agent behavior is configured via `agents.toml` files, loaded from both
global and project locations (`crates/ava-config/src/agents.rs`):

```
~/.ava/agents.toml      (global defaults)
.ava/agents.toml        (project overrides)
```

Format:

```toml
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 10
enabled = true

[agents.task]
enabled = true
max_turns = 15
prompt = "You are a focused sub-agent..."

[agents.review]
enabled = true
model = "anthropic/claude-sonnet-4"
max_turns = 5
```

Fields:
- `enabled` -- Whether this agent type can be spawned (default: true)
- `model` -- Model override for this agent (default: inherits from parent)
- `max_turns` -- Maximum turns (default: 10, capped at parent's limit)
- `prompt` -- Custom system prompt (default: built-in sub-agent prompt)

The `AgentsConfig::get_agent(name)` method resolves overrides by merging
per-agent settings with defaults.

## Output Format

Loaded instructions are concatenated with section headers showing their source:

```
# Project Instructions

Follow the instructions below for this project.

# From: /home/user/.ava/AGENTS.md

Global user rules here.

# From: /home/user/project/AGENTS.md

Project-specific rules here.

# From: /home/user/project/.ava/rules/style.md

Style guidelines here.
```

Startup instructions are appended to the system prompt suffix in
`AgentStack::run()`. Matching rule bodies are added later via tool-result
context after file touches.

## Key Files

| File | Role |
|---|---|
| `crates/ava-agent/src/instructions.rs` | Discovery, startup loading, rule matching, contextual instructions |
| `crates/ava-agent/src/instruction_resolver.rs` | Startup prompt suffix assembly and trimming |
| `crates/ava-config/src/agents.rs` | `AgentsConfig`, `AgentDefaults`, `AgentOverride` |
| `crates/ava-config/src/lib.rs` | `instructions` field in config.yaml |
