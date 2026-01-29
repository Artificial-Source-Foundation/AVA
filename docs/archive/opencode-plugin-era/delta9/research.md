# AI Documentation Research Summary

> Research findings on AI-readable documentation best practices.

---

## Standards Researched

### 1. llms.txt

**Source**: [llmstxt.org](https://llmstxt.org/)

**Purpose**: A standard for providing AI-friendly content indexes on websites.

**Format**:
```markdown
# Project Name

> One-line summary

## Section 1
- [Link](url): Description

## Section 2
- [Link](url): Description
```

**Key Points**:
- Place at project root
- H1 title + blockquote summary
- H2 sections with markdown links
- Brief descriptions for each link
- Machine-parseable structure

---

### 2. AGENTS.md

**Source**: [agents.md](https://agents.md/) and [GitHub Blog](https://github.blog/ai-and-ml/github-copilot/how-to-write-a-great-agents-md-lessons-from-over-2500-repositories/)

**Purpose**: Agent-specific instructions complementing README.md.

**Key Insights from GitHub's Analysis (2500+ repos)**:
- No required schema - use any Markdown structure
- Common sections: commands, testing, code style, boundaries
- Keep under 150 lines to avoid burying signal
- Include "Always Do / Ask First / Never Do" boundaries
- Put build/test commands early (most commonly needed)
- Agents automatically run programmatic checks listed

**Example Structure**:
```markdown
# Project Agent Instructions

## Commands
npm run build
npm run test

## Code Style
- TypeScript strict mode
- Use Zod for validation

## Boundaries
### Always Do
- Read docs before changes
### Ask First
- Modifying schemas
### Never Do
- Use any type
```

---

### 3. CLAUDE.md

**Source**: [Anthropic Engineering](https://www.anthropic.com/engineering/claude-code-best-practices)

**Purpose**: Project instructions for Claude Code.

**Key Best Practices**:

1. **Keep concise** - Treat as a prompt, not a novel
2. **Iterate** - Refine like any frequently-used prompt
3. **Use emphasis** - "IMPORTANT" or "YOU MUST" improves adherence
4. **Hierarchical files** - Root + subdirectory-specific files
5. **Quick memory** - Use `#` to add rules during session
6. **Check in** - Share via git for team consistency
7. **Use CLAUDE.local.md** - For personal, untracked customizations

**File Locations**:
- `CLAUDE.md` - Project root (recommended, checked in)
- `CLAUDE.local.md` - Personal, .gitignored
- Subdirectory `CLAUDE.md` files for monorepos

---

### 4. .cursorrules / .cursor/rules/

**Source**: [awesome-cursorrules](https://github.com/PatrickJS/awesome-cursorrules)

**Status**: `.cursorrules` is deprecated; prefer `.cursor/rules/*.mdc`

**Key Points**:
- Under 500 lines total
- Use MDC (Markdown Components) format in new system
- Pattern-based activation
- Can include code examples

---

## Best Practices Summary

### Content Organization

| Priority | Content |
|----------|---------|
| 1 | Commands (build, test, lint) |
| 2 | Project structure |
| 3 | Key concepts |
| 4 | Boundaries (do/ask/never) |
| 5 | Common tasks |
| 6 | Links to detailed docs |

### Writing Style

- **Front-load** - Most important info first
- **Scannable** - Headers, bullets, tables
- **Specific** - Exact paths, exact commands
- **Concise** - Respect token limits
- **Link, don't duplicate** - Point to detailed docs
- **Include examples** - Code snippets for patterns

### File Sizes

| File | Recommended Size |
|------|------------------|
| CLAUDE.md | 100-200 lines |
| AGENTS.md | Under 150 lines |
| llms.txt | 50-100 lines |
| Per-file docs | 200-400 lines |

---

## Sources

### Official Documentation
- [llms.txt Specification](https://llmstxt.org/)
- [AGENTS.md Standard](https://agents.md/)
- [Claude Code Best Practices](https://www.anthropic.com/engineering/claude-code-best-practices)
- [How to write a great AGENTS.md](https://github.blog/ai-and-ml/github-copilot/how-to-write-a-great-agents-md-lessons-from-over-2500-repositories/)

### Community Resources
- [awesome-cursorrules](https://github.com/PatrickJS/awesome-cursorrules)
- [ClaudeLog](https://claudelog.com/)
- [Builder.io Claude Code Guide](https://www.builder.io/blog/claude-code)

### Reference Implementations
- [oh-my-opencode](https://github.com/code-yeongyu/oh-my-opencode)
- [anthropic-cookbook](https://github.com/anthropics/anthropic-cookbook)

---

## Application to Delta9

Based on this research, Delta9 documentation includes:

| File | Standard | Purpose |
|------|----------|---------|
| `CLAUDE.md` | Claude Code | Primary AI entry point |
| `AGENTS.md` | agents.md | Universal agent instructions |
| `llms.txt` | llmstxt.org | AI-readable index |
| `.cursorrules` | Cursor | Legacy compatibility |
| `llms-full.txt` | - | Complete content for RAG |
| `docs/REFERENCE_CODE/` | - | Actual plugin source code |

This multi-standard approach ensures compatibility with:
- Claude Code
- OpenCode
- Cursor
- GitHub Copilot
- Other AI coding assistants
