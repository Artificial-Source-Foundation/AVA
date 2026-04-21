---
title: "Instructions And Skills"
description: "How AVA discovers project instructions, modular rules, includes, and SKILL.md files."
order: 6
updated: "2026-04-20"
---

# Instructions And Skills

Instructions and skills are AVA's primary behavior-shaping layer inside a repo.

## Project Instructions

AVA auto-discovers instruction files and injects them into the system prompt. These are not plugins, but they are the main way to customize behavior per project.

### Discovery Order

1. `~/.ava/AGENTS.md` - global instructions
2. Trusted project `AGENTS.md` chain from the repository or trusted boundary down to the active working directory
3. Boundary-root files: `.cursorrules`, `.github/copilot-instructions.md`
4. Boundary-root `.ava/AGENTS.md` - project-local override
5. `.ava/rules/*.md` - modular rules loaded alphabetically
6. `instructions:` paths or globs in `config.yaml`
7. Skill files from filesystem-discovered `.claude/skills/`, `.agents/skills/`, and `.ava/skills/` directories (loaded via `SKILL.md`)
8. Local overrides such as `AGENTS.local.md` and `.ava/AGENTS.local.md`, loaded last

Files are plain Markdown. Each is prefixed in the prompt with `# From: <filepath>`, duplicate paths are deduplicated, and `@path` include directives are supported with recursion and size limits. Project-local instruction discovery stays inside the explicitly trusted project root. When AVA is launched from a subdirectory, it now carries the full trusted `AGENTS.md` stack from that boundary down to the current working directory at startup, then lazily loads any deeper nested `AGENTS.md` files as tools touch files below the current directory. Global/home instructions keep normal filesystem-relative include behavior.

## Skills

Skills are reusable `SKILL.md` instruction files that provide focused workflows or domain-specific guidance. In this repo they are especially useful for Cloudflare, React, security review, documentation, and debugging tasks.

AVA now exposes the live discovered runtime skill set through `/skills` (and `/skills list`). The command lists the actual `SKILL.md` files currently visible to the runtime: global skills are always shown, while project-local skills only appear when the workspace is trusted.

## Implementation

Source: `crates/ava-agent/src/instructions.rs`
