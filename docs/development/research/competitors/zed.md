# Zed

> High-performance AI-native code editor (~50k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Zed is a **high-performance AI-native code editor** built in **Rust**. It's designed from the ground up for collaboration and AI integration, not a plugin on top of an existing editor.

**Key architectural decisions:**
- **Rust core** — Memory safety, performance, parallelism
- **CRDT-based collaboration** — Real-time collaborative editing
- **AI-native** — Built-in AI features, not bolted on
- **GPU-accelerated UI** — Custom UI framework

### Project Structure

```
zed/
├── crates/
│   ├── zed/                 # Main editor
│   ├── assistant/           # AI assistant
│   ├── collab/              # Collaboration
│   ├── editor/              # Editor core
│   └── gpui/                # UI framework
└── ...
```

---

## Key Patterns

### 1. Rust Performance

Rust core for:
- Memory safety without GC
- Zero-cost abstractions
- Easy parallelism
- Predictable performance

### 2. CRDT Collaboration

Conflict-free Replicated Data Types:
- Real-time collaboration
- Offline support
- Automatic conflict resolution

### 3. AI-Native Design

AI features built-in:
- Context-aware suggestions
- Inline completions
- Natural language commands
- Not plugins — core features

### 4. GPU-Accelerated UI

Custom GPUI framework:
- GPU-rendered text
- Smooth animations
- High frame rates

### 5. Agent Panel

Dedicated UI for AI agents:
- Chat interface
- Context visualization
- Tool execution view
- Progress tracking

---

## What AVA Can Learn

### High Priority

1. **AI-Native Design** — Build AI features as core, not plugins.

2. **Performance** — Rust enables smooth, responsive UI.

3. **Collaboration** — CRDTs enable real-time collaboration.

### Medium Priority

4. **Context Awareness** — Deep editor integration enables better AI context.

5. **GPU Acceleration** — Smooth UI improves perceived performance.

---

## Comparison: Zed vs AVA

| Capability | Zed | AVA |
|------------|-----|-----|
| **Type** | Editor | Agent |
| **Language** | Rust | TypeScript/Rust |
| **AI integration** | Native | Extensions |
| **Collaboration** | Built-in | Not yet |
| **Performance** | Excellent | Good |
| **Use case** | Daily editing | Complex tasks |

---

*Consolidated from: audits/zed-audit.md, backend-analysis/zed.md*
