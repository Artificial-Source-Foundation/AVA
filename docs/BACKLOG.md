# Delta9 Backlog

> Future enhancements and feature requests

---

## Test 2 Complete ✅

All bugs from Test 2 have been fixed. See [COMPLETED.md](COMPLETED.md) for details.

| Sprint | Status |
|--------|--------|
| Sprint 1-3 | ✅ Robustness overhaul |
| Sprint 4 | ✅ BUG-12, 13, 14, 15, 16 |
| Sprint 5 | ✅ BUG-10, CLEANUP-1 |

**Ready for Test 3!**

---

## Future Enhancements

### HIGH Priority

#### ENH-2: Auto-Sync delegate_task with Mission ✅ DONE (BUG-14)

Implemented in Sprint 4.

#### ENH-3: Agent Auto-Fallback on Failure ✅ DONE (BUG-16)

Implemented circuit breaker in `src/lib/agent-fallback.ts`.

#### ENH-4: Better Error Messages 🟠

Improve error messages with actionable context:
- "JSON Parse error: Unexpected EOF" → "Gemini returned incomplete response. Consider using operator instead."
- "Agent not found" → "Agent 'ui_ops' not registered. Available: commander, operator, validator"

---

### MEDIUM Priority

#### ENH-5: Unified run_task Tool

Single tool that replaces dispatch_task + delegate_task confusion:

```typescript
run_task({
  taskId: 'task_xyz',  // Optional - syncs with mission if provided
  prompt: '...',
  agent: 'auto',       // Auto-routes to best agent
  background: true     // Auto-tracks in background
})
```

#### ENH-6: execute_objective Tool

Run all tasks in an objective automatically with dependency handling.

#### ENH-7: Mission Progress Sync Command

Tool `mission_sync` that scans git changes and updates task statuses.

---

### LOW Priority

| ID | Request |
|----|---------|
| ENH-8 | Live Mission Dashboard TUI |
| ENH-9 | Agent Performance Metrics |
| ENH-10 | Background Task Health Monitoring |
| ENH-11 | Task Replay/Debug |
| ENH-12 | Dry Run Mode |
| ENH-13 | Resumable Missions |
| ENH-14 | Learned Patterns |
| ENH-15 | Smart Checkpointing |
| ENH-16 | Squadron Templates |
| ENH-17 | Human Checkpoints |

---

### ARCHITECTURE

#### ENH-18: Event-Driven Architecture

Replace polling with event bus:
- Tasks emit: started, progress, completed, failed
- Commander subscribes and reacts
- UI can also subscribe for live updates

---

## External Issues

### BUG-11: Background Task Visibility (OpenCode)

CTRL+X navigation doesn't show plugin-created sessions. Filed as OpenCode platform issue.

---

## References

- [COMPLETED.md](COMPLETED.md) - Completed work archive
- [spec.md](spec.md) - Full specification
- [CLAUDE.md](../CLAUDE.md) - Project overview
