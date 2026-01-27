# Delta9 Backlog

> Active work and known issues

---

## Status

| Category | Count | Status |
|----------|-------|--------|
| **Completed Phases** | 7 | See [COMPLETED.md](COMPLETED.md) |
| **Launch Tasks** | 2 | Pending |
| **Testing Issues** | 7+ | In Triage |

**Last Updated:** 2026-01-26

---

## Phase 6: Launch

| ID | Task | Status | Notes |
|----|------|--------|-------|
| L-1 | npm publish | ⬜ Pending | Package as `delta9` |
| L-3 | Marketing | ⬜ Pending | README, social, plugin directory |

---

## Testing Issues (Live Testing 2026-01-26)

### Critical 🔴

| ID | Issue | Root Cause | Proposed Fix |
|----|-------|------------|--------------|
| BUG-1 | **Council fails silently** | Unknown (not GPT rate limit) | Investigate council config |
| BUG-2 | **No fallback models for Council** | Oracles have no fallback chain | Add per-Oracle fallbacks |
| BUG-3 | **No fallback models for Delta Team** | Support agents have no fallbacks | Add per-agent fallbacks |

### Medium 🟡

| ID | Issue | Root Cause | Proposed Fix |
|----|-------|------------|--------------|
| BUG-4 | **Subagent timeouts too short** | Fixed 120s, complex tasks need more | Adaptive timeout estimation |
| BUG-5 | **No timeout estimation** | AI guesses, often wrong | `estimateTimeout(agent, prompt)` |
| BUG-6 | **Council shows "simulation mode"** | SDK not connected or config issue | Better error messaging |

### Low 🟢

| ID | Issue | Root Cause | Proposed Fix |
|----|-------|------------|--------------|
| BUG-7 | **Squadron status unclear on timeout** | No per-agent timeout status | Add to `squadron_status` |

### To Be Triaged ⬜

*(Add new issues here as testing continues)*

| ID | Issue | Notes |
|----|-------|-------|
| | | |

---

## Proposed Solutions

### Fallback Models (BUG-2, BUG-3)

```typescript
// Oracle fallbacks
const ORACLE_FALLBACKS = {
  CIPHER: ['claude-sonnet-4', 'gpt-4o', 'gemini-2.0-flash'],
  VECTOR: ['gpt-4o', 'claude-sonnet-4', 'gemini-2.0-flash'],
  PRISM: ['gemini-2.0-flash', 'claude-sonnet-4', 'gpt-4o'],
  APEX: ['deepseek-chat', 'claude-sonnet-4', 'gpt-4o'],
}

// Delta Team fallbacks
const AGENT_FALLBACKS = {
  RECON: ['claude-haiku', 'gpt-4o-mini', 'gemini-flash'],
  SIGINT: ['claude-sonnet-4', 'gpt-4o', 'gemini-2.0-flash'],
  TACCOM: ['gpt-4o', 'claude-sonnet-4', 'gemini-2.0-flash'],
  SURGEON: ['claude-haiku', 'gpt-4o-mini'],
  SENTINEL: ['claude-sonnet-4', 'gpt-4o'],
  SCRIBE: ['gemini-flash', 'claude-haiku'],
  FACADE: ['gemini-flash', 'claude-sonnet-4'],
  SPECTRE: ['gemini-flash', 'gpt-4o'],
}
```

### Adaptive Timeouts (BUG-4, BUG-5)

```typescript
function estimateTimeout(agentType: string, prompt: string): number {
  const baseTimeouts = {
    scout: 60_000,      // 1 min
    intel: 180_000,     // 3 min
    operator: 300_000,  // 5 min
    validator: 120_000, // 2 min
  }

  const words = prompt.split(' ').length
  const multiplier = words > 200 ? 2 : words > 100 ? 1.5 : 1

  return Math.min(baseTimeouts[agentType] * multiplier, 600_000)
}
```

---

## Test Results Summary

### Working ✅

- Commander delegation (never writes code)
- Mission state creation and tracking
- Delta Team spawning (RECON, SIGINT, TACCOM)
- Parallel subagent deployment
- Model selection (Opus for Commander)
- Background task management
- Graceful degradation on timeout
- Operator execution
- Validator gate

### Not Working ❌

- Council deliberation (fails silently)
- Squadron completion (timeouts)

### Untested ⬜

- Process cleanup (Ctrl+C)
- Session isolation
- Compliance violation hooks
- Context compaction recovery

---

## Next Steps

1. **Investigate Council failure** (BUG-1) - Check config, SDK connection
2. **Add more test failures** - As user reports them
3. **Prioritize fixes** - After triage complete
4. **Fix critical issues first** - BUG-1, BUG-2, BUG-3

---

## References

- [COMPLETED.md](COMPLETED.md) - Completed phases archive
- [spec.md](spec.md) - Full specification
- [CLAUDE.md](../CLAUDE.md) - Project overview
