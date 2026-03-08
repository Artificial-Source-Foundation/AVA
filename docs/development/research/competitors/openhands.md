# OpenHands

> Open-source AI software engineering agent (~40k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

OpenHands is an **open-source AI software engineering agent** designed for complex development tasks. It's built as a modular Python system with a web-based UI.

**Key architectural decisions:**
- **Micro-agent architecture** — Specialized agents for different tasks
- **Action/Observation loop** — Clean separation between agent actions and environment observations
- **Event-driven** — Async event system for communication
- **Runtime environment** — Sandboxed execution via Docker

### Project Structure

```
openhands/
├── openhands/
│   ├── core/                # Core logic
│   ├── events/              # Event system
│   ├── runtime/             # Execution environment
│   ├── agenthub/            # Agent implementations
│   └── server/              # Web server
├── frontend/                # React UI
└── ...
```

---

## Key Patterns

### 1. Action/Observation Loop

Clean separation:
- **Actions** — What the agent wants to do (read file, run command)
- **Observations** — Results from the environment
- **Events** — Async communication between components

### 2. Micro-Agents

Specialized agents for different tasks:
- **Coder** — Code generation
- **Reviewer** — Code review
- **Planner** — Task planning
- **Tester** — Test generation

### 3. Runtime Environment

Docker-based sandbox:
- Isolated from host
- Customizable base images
- Persistent state

### 4. Web-Based UI

React frontend with:
- Real-time updates via WebSocket
- File browser
- Terminal access
- Chat interface

---

## What AVA Can Learn

### High Priority

1. **Micro-Agent Architecture** — Specialized agents for different tasks.

2. **Action/Observation Loop** — Clean separation improves testing and modularity.

3. **Docker Sandboxing** — Runtime isolation improves security.

### Medium Priority

4. **Event-Driven** — Async events enable better parallelism.

---

## Comparison: OpenHands vs AVA

| Capability | OpenHands | AVA |
|------------|-----------|-----|
| **Architecture** | Micro-agents | Modular extensions |
| **Language** | Python | TypeScript/Rust |
| **Sandbox** | Docker | Docker (optional) |
| **UI** | Web-based | Desktop (Tauri) |
| **Loop** | Action/Observation | Tool calling |

---

*Consolidated from: audits/openhands-audit.md, backend-analysis/openhands.md*
