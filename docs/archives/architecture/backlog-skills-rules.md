# Backlog: Skills & Rules Enhancement

> Research-driven backlog for evolving AVA's skills system and adding a rules system.
> Based on competitive analysis of Claude Code, OpenCode, Aider, Continue.dev, and Cursor.

---

## Current State (Updated 2026-03-03)

### What We Have (Skills)
- **Built-in skills**: 8 domain skills (React, Python, Rust, Go, TS, CSS, Docker, SQL)
- **Custom skills**: Users create via Settings UI (name, description, file globs, instructions)
- **Activation**: Glob-based matching at `agent:turn-start`, max 5 active skills
- **Activation modes**: `auto` (glob-based, default), `always` (every turn), `manual` (explicit only)
- **Backend**: `packages/extensions/skills/` — `load_skill` tool, init generator, SKILL.md format
- **File locations**: `.ava/skills/`, `.claude/skills/`, `.agents/skills/` with YAML frontmatter
- **Shared frontmatter parser**: `packages/extensions/skills/src/frontmatter.ts` (used by skills + rules)
- **UI**: Full CRUD in Settings → Skills tab (edit/delete built-in, create custom, toggle, activation mode selector)
- **AI-assisted creation**: `create_skill` tool in `tools-extended/` — generates skills from conversation context

### What We Have (Rules) — NEW
- **Rules extension**: `packages/extensions/rules/` with loader, matcher, activation, prompt injection
- **File locations**: `.ava/rules/`, `.claude/rules/`, `.cursor/rules/` directories
- **Frontmatter format**: YAML with `globs`, `description`, `activation` (`auto`/`always`/`manual`)
- **Glob matching**: Auto-activates rules when files in context match rule globs
- **Prompt injection**: Priority 140 via `addPromptSection()` hook
- **AI-assisted creation**: `create_rule` tool in `tools-extended/` — generates rules mid-conversation
- **UI**: Rules CRUD in Settings → Skills & Rules tab (`rules-section.tsx`: list, edit, delete, create)
- **Tests**: Full coverage (loader, matcher, index tests)

### What We Have (Instructions)
- **Project-level**: AGENTS.md, CLAUDE.md loaded at session start
- **Subdirectory**: Walking up directories for context-specific instructions
- **URL loader**: Remote instruction loading from URLs
- **Backend**: `packages/extensions/instructions/` — loader, subdirectory, url-loader, init

### What We Have (Slash Commands) — NEW
- **CLI integration test**: 11 built-in commands verified (help, clear, mode, model, compact, undo, redo, settings, status, export, init)
- **Command resolver service**: `src/services/command-resolver.ts` — bridges core-v2 registry to desktop
- **Slash command popover**: `src/components/chat/message-input/slash-command-popover.tsx` — autocomplete UI with keyboard nav, grouped by built-in vs custom

---

## Competitive Landscape

### Claude Code (3-tier system)
| Layer | File | Behavior |
|-------|------|----------|
| **Instructions** | `CLAUDE.md` | Always loaded, walks up dirs, supports `@import` |
| **Rules** | `.claude/rules/*.md` | YAML frontmatter with `globs`, `description`; auto-applied when paths match |
| **Skills** | `.claude/skills/*.md` | LLM reads description → decides to invoke; not auto-applied |

Key features:
- Rules have `globs` patterns for auto-activation on matching files
- Skills are LLM-invoked: agent sees skill descriptions, decides when to use them
- `@import` directive for composing instruction files
- Local overrides: `CLAUDE.local.md` (gitignored)

### OpenCode (agent-centric)
- **Agents**: Named personas with custom instructions + allowed tools
- **Commands**: `/command` with `$ARGUMENTS` substitution
- **Skills**: Context-dependent prompt injection with glob patterns
- **Custom tools**: Shell-based tools (`` !`cmd` `` injection, `context: fork` for subagents)

### Cursor (.cursor/rules/)
- Migrated from `.cursorrules` → `.cursor/rules/` directory
- Each rule file has: description, globs, auto-attach behavior
- "Always", "Auto", "Agent Requested", "Manual" activation modes
- AI-assisted rule creation: "Generate Cursor Rules" from chat

### Continue.dev
- `.continue/rules/` with glob patterns
- `create_rule_block` tool — AI can create rules mid-conversation
- Supports system-level, user-level, and project-level rules
- Negative globs for exclusion

### Aider
- Simple `.aider.conf.yml` conventions files
- Minimalist approach — no structured skills/rules

### Vercel v0
- Public skills page with community-contributed skills
- Potential source for remote skill fetching/compatibility

---

## Enhancement Status

### P0 — Rules System ✅ DONE

Path-targeted, always-on instructions that auto-activate based on file context.

- [x] Define `Rule` type with id, name, description, globs, content, activation modes
- [x] File format: `.ava/rules/*.md` with YAML frontmatter
- [x] Cross-tool compat: scans `.ava/rules/`, `.claude/rules/`, `.cursor/rules/`
- [x] Glob matching: inject rule content into prompt when files match
- [x] Settings UI: Rules section in Skills & Rules tab — list, toggle, edit, create
- [x] Activation modes: `always`, `auto` (default), `manual`
- [ ] Negative globs support (`!*.test.ts`)
- [ ] Priority ordering when multiple rules match

### P1 — AI-Assisted Skill & Rule Creation ✅ MOSTLY DONE

- [x] `create_skill` tool — AI generates a skill from conversation context
- [x] `create_rule` tool — AI generates a rule mid-conversation
- [x] Interactive refinement: AI proposes → user reviews → saves to `.ava/skills/` or `.ava/rules/`
- [ ] Suggest skill/rule creation when AI detects repeated patterns
- [ ] Settings UI: "Ask AI to create" button in Skills/Rules tabs

### P1 — LLM-Based Skill Activation ⚠️ PARTIAL

Agent sees skill descriptions and decides when to invoke (like Claude Code).

- [x] Activation modes per skill: `auto`, `always`, `manual` implemented
- [ ] `agent` activation mode — LLM decides based on description (not yet implemented)
- [ ] Expose skill catalog to agent as tool descriptions (name + description only)
- [ ] Skill budget: max tokens allocated to active skills, smart prioritization

### P2 — Remote & Community Skills — NOT STARTED

- [ ] Vercel v0 skills page: parse and import compatible skills
- [ ] Skill format compatibility layer (convert Cursor rules, Claude Code skills to AVA format)
- [ ] Remote skill URL in frontmatter: `source: https://...`
- [ ] Skill marketplace in plugin catalog (extend existing catalog infrastructure)
- [ ] `@import` directive in skills/rules (like Claude Code's CLAUDE.md imports)
- [ ] Periodic sync for remote skills (configurable interval)

### P2 — Advanced Skill Features — NOT STARTED

- [ ] String substitution: `$ARGUMENTS` (full input), `$1`, `$2` (positional args)
- [ ] Shell command injection: `` !`git log --oneline -5` `` embeds command output
- [ ] Subagent execution: `context: fork` — skill runs in isolated subagent
- [ ] Skill chaining: one skill can invoke another
- [ ] Skill templates: parameterized skills with `{{ variable }}` placeholders

### P2 — Local Overrides — NOT STARTED

- [ ] `.ava/local/` directory (gitignored by default)
- [ ] `AGENTS.local.md` / `CLAUDE.local.md` — merged after main instructions
- [ ] Local skill overrides: same ID as project skill → personal version wins
- [ ] `.gitignore` auto-entry when local files are created

### P3 — Skill Invocation UX — NOT STARTED

- [ ] Active skills indicator in chat UI (show which skills are injected)
- [ ] "Why was this skill activated?" explainer
- [ ] Skill token budget display (how much context each skill consumes)
- [ ] Quick-toggle skills from chat sidebar
- [ ] Skill conflict detection (overlapping globs, contradictory instructions)

---

## Implementation Notes

### File Format (SKILL.md with YAML Frontmatter)
```markdown
---
name: React Patterns
description: Component composition and hooks best practices
globs:
  - "**/*.tsx"
  - "**/*.jsx"
activation: auto
---

Follow React 19 patterns: use functional components, hooks, proper key usage...
```

### Rule Format (.ava/rules/*.md)
```markdown
---
description: Enforce testing conventions for test files
globs:
  - "**/*.test.ts"
  - "**/*.spec.ts"
activation: auto
---

When writing tests:
- Use `describe`/`it` blocks
- Mock external dependencies
- Test edge cases and error paths
```

### Cross-Tool Compatibility Matrix

| Feature | AVA | Claude Code | Cursor | Continue.dev | OpenCode |
|---------|-----|-------------|--------|--------------|----------|
| Always-on instructions | AGENTS.md | CLAUDE.md | .cursorrules | .continue/config | instructions |
| Path-targeted rules | **DONE** | .claude/rules/ | .cursor/rules/ | .continue/rules/ | skills |
| LLM-invoked skills | **Partial** (glob only) | .claude/skills/ | Agent Requested | N/A | agents |
| AI-assisted creation | **DONE** | N/A | "Generate Rules" | create_rule_block | agent create |
| Remote/community | TODO | N/A | N/A | N/A | N/A |
| String substitution | TODO | N/A | N/A | N/A | $ARGUMENTS |
| Shell injection | TODO | N/A | N/A | N/A | !`cmd` |
| Subagent execution | TODO | N/A | N/A | N/A | context: fork |
| Local overrides | TODO | CLAUDE.local.md | N/A | N/A | N/A |

---

## Affected Files

| Area | Files |
|------|-------|
| Skills extension | `packages/extensions/skills/src/` |
| Rules extension | `packages/extensions/rules/src/` (loader, matcher, index + tests) |
| Shared frontmatter | `packages/extensions/skills/src/frontmatter.ts` |
| Instructions extension | `packages/extensions/instructions/src/` |
| AI creation tools | `packages/extensions/tools-extended/src/create-skill.ts`, `create-rule.ts` |
| Skills & Rules UI | `src/components/settings/tabs/SkillsTab.tsx`, `skills-tab-card.tsx`, `skills-tab-data.ts`, `rules-section.tsx` |
| Settings types | `src/stores/settings/settings-types.ts` |
| Slash commands | `src/services/command-resolver.ts`, `src/components/chat/message-input/slash-command-popover.tsx` |
| Prompts extension | `packages/extensions/prompts/src/` (inject rules into system prompt) |

---

## Open Questions

1. ~~Should rules live in the same `.ava/skills/` directory or separate `.ava/rules/`?~~ **Answered**: Separate `.ava/rules/` directory.
2. Should we support Cursor's `.cursorrules` file directly (single-file, no frontmatter)?
3. How to handle token budget conflicts between skills, rules, and instructions?
4. Should the Vercel skills integration be a built-in or a community plugin?
5. ~~Should `create_skill`/`create_rule` tools be always available or only in plan mode?~~ **Answered**: Always available.
