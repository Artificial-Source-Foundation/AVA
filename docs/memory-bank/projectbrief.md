# Project Brief

> Core definition of Estela - rarely changes

---

## What is Estela?

A **desktop AI coding assistant** built with Tauri + SolidJS. Multi-agent system where Commander plans, Operators execute, and Validator verifies.

**Name meaning:** Spanish for "star trail" or "wake"

---

## Core Goals

1. **Multi-provider LLM support** - OpenRouter, Anthropic, OpenAI, GLM
2. **Hierarchical agents** - Commander (plans) → Operators (code) → Validator (QA)
3. **Local-first** - SQLite database, runs on desktop
4. **Parallel execution** - Multiple operators working simultaneously

---

## Tech Stack

| Layer | Technology |
|-------|------------|
| Desktop | Tauri 2.0 |
| Frontend | SolidJS + TypeScript |
| Styling | Tailwind CSS v4 |
| Database | SQLite |
| LLM | Multi-provider (frontend-first) |

---

## Non-Goals (for now)

- Cloud sync
- Collaboration features
- Mobile app
- Plugin system

---

## Success Criteria

- [ ] Can have streaming chat with multiple LLM providers
- [ ] Can read/write files via agents
- [ ] Commander delegates tasks to Operators
- [ ] Validator catches errors before completion
- [ ] Parallel operators for speed
