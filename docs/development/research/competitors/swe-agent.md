# SWE-Agent

> Agent for software engineering tasks (~15k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

SWE-Agent is an **AI agent specialized for software engineering tasks**, particularly bug fixing and feature implementation. Built by researchers at Princeton, it's designed for accuracy on SWE-bench benchmarks.

**Key architectural decisions:**
- **Specialized for code tasks** — Optimized for bug fixing and feature work
- **ReAct-style prompting** — Reasoning and acting in interleaved steps
- **Interactive environment** — Stateful interaction with codebase
- **Research-backed** — Designed based on academic research

### Project Structure

```
swe-agent/
├── sweagent/
│   ├── agent/               # Agent implementations
│   ├── environment/         # Code interaction environment
│   ├── models/              # LLM integrations
│   └── ...
├── config/                  # Agent configurations
└── ...
```

---

## Key Patterns

### 1. ReAct Prompting

Reasoning + Acting:
- **Thought** — Agent thinks about the problem
- **Action** — Agent takes action (read, edit, run)
- **Observation** — Agent observes results
- **Repeat** — Continue until solved

### 2. Specialized Tools

Tools designed for software engineering:
- `search` — Find code patterns
- `view` — View file contents
- `edit` — Make precise edits
- `bash` — Run commands

### 3. Interactive Environment

Stateful interaction:
- Maintains context across turns
- Tracks file states
- Handles command history

### 4. Research-Backed Design

Based on academic research:
- Optimized prompts for code tasks
- Structured reasoning patterns
- Benchmark-driven improvements

---

## What AVA Can Learn

### High Priority

1. **ReAct Prompting** — Explicit reasoning improves accuracy.

2. **Specialized Tools** — Tools designed for specific domains work better.

3. **Research-Backed** — Benchmark-driven development improves quality.

---

## Comparison: SWE-Agent vs AVA

| Capability | SWE-Agent | AVA |
|------------|-----------|-----|
| **Focus** | Bug fixing | General coding |
| **Approach** | ReAct | Tool calling |
| **Research** | Academic | Production |
| **Benchmark** | SWE-bench | Real-world |

---

*Consolidated from: audits/swe-agent-audit.md, backend-analysis/swe-agent.md*
