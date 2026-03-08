# Pi Mono

> Minimal AI coding assistant (~5k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Pi Mono is a **minimal AI coding assistant** focused on simplicity and reliability. It's designed as a lightweight tool for common coding tasks.

**Key architectural decisions:**
- **Minimal feature set** — Only essential features
- **Simple architecture** — Easy to understand and extend
- **Single-file focus** — Works on one file at a time
- **Language agnostic** — Works with any language

---

## Key Patterns

### 1. Minimalism

Only essential features:
- Read file
- Edit file
- Run command
- No complex context management

### 2. Single-File Focus

Works on one file at a time:
- Simpler mental model
- No project-wide complexity
- Faster operations

### 3. Language Agnostic

Works with any language:
- No language-specific parsing
- Text-based operations only
- Universal applicability

---

## What AVA Can Learn

### High Priority

1. **Simplicity** — Don't over-engineer. Sometimes minimal is better.

2. **Single-File Focus** — Option to work on single files for simple tasks.

---

## Comparison: Pi Mono vs AVA

| Capability | Pi Mono | AVA |
|------------|---------|-----|
| **Scope** | Single file | Project-wide |
| **Features** | Minimal | Rich |
| **Complexity** | Low | High |
| **Use case** | Quick edits | Complex tasks |

---

*Consolidated from: audits/pi-mono-audit.md, backend-analysis/pi-mono.md*
