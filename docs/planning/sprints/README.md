# AVA Backend Sprint Backlog 2026 - Modular

> 9-month direct-to-Rust sprint plan, organized by epic

## Structure

```
docs/planning/sprints/
├── epic-1/                    # Foundation (Sprints 24-26)
│   ├── sprint-24.md          # Workspace & Types
│   ├── sprint-25.md          # Infrastructure
│   └── sprint-26.md          # Core Foundation
├── epic-2/                    # Essential Tools (Sprints 27-29)
│   ├── sprint-27.md          # Edit Tool Excellence
│   ├── sprint-28.md          # Search & Context
│   └── sprint-29.md          # LSP & Sandboxing
├── epic-3/                    # Agent Core (Sprints 30-32)
│   ├── sprint-30.md          # Agent Loop
│   ├── sprint-31.md          # Commander & LLM
│   └── sprint-32.md          # MCP & Integration
├── epic-4/                    # Complete Backend (Sprints 33-35)
│   ├── sprint-33.md          # Remaining Tools
│   ├── sprint-34.md          # Extensions & Validation
│   └── sprint-35.md          # Performance & Polish
├── epic-5/                    # Frontend Integration (Sprints 36-38)
│   ├── sprint-36.md          # Tauri Integration
│   ├── sprint-37.md          # Frontend Updates
│   └── sprint-38.md          # Testing & Migration
└── epic-6/                    # Ship It (Sprints 39-41)
    ├── sprint-39.md          # Bug Fixes
    ├── sprint-40.md          # Performance
    └── sprint-41.md          # Release
```

## Quick Reference

| Sprint | Epic | Focus | Stories | Points |
|--------|------|-------|---------|--------|
| 24 | 1 | Workspace & Types | 3 | 20 |
| 25 | 1 | Infrastructure | 3 | 32 |
| 26 | 1 | Core Foundation | 3 | 24 |
| 27 | 2 | Edit Tool Excellence | 3 | 44 |
| 28 | 2 | Search & Context | 3 | 44 |
| 29 | 2 | LSP & Sandboxing | 3 | 44 |
| 30 | 3 | Agent Loop | 3 | 40 |
| 31 | 3 | Commander & LLM | 3 | 40 |
| 32 | 3 | MCP & Integration | 3 | 40 |
| 33 | 4 | Remaining Tools | 4 | 44 |
| 34 | 4 | Extensions & Validation | 3 | 36 |
| 35 | 4 | Performance & Polish | 3 | 36 |
| 36 | 5 | Tauri Integration | 3 | 32 |
| 37 | 5 | Frontend Updates | 3 | 32 |
| 38 | 5 | Testing & Migration | 3 | 24 |
| 39 | 6 | Bug Fixes | 2 | 30 |
| 40 | 6 | Performance | 2 | 30 |
| 41 | 6 | Release | 2 | 30 |
| **Total** | **6 Epics** | **18 Sprints** | **51 Stories** | **642 Points** |

## Epic Summary

### Epic 1: Foundation (Sprints 24-26)
- **Goal:** Core Rust infrastructure
- **Deliverables:** Workspace, types, platform, DB, shell, FS
- **Points:** 76

### Epic 2: Essential Tools (Sprints 27-29)
- **Goal:** Best-in-class tools
- **Deliverables:** Edit strategies, BM25, PageRank, condensers, LSP, sandboxing
- **Points:** 132

### Epic 3: Agent Core (Sprints 30-32)
- **Goal:** Agent loop and orchestration
- **Deliverables:** Agent loop, tool registry, commander, LLM providers, MCP
- **Points:** 120

### Epic 4: Complete Backend (Sprints 33-35)
- **Goal:** All remaining tools and polish
- **Deliverables:** Git, browser, memory, permissions, extensions, validation
- **Points:** 116

### Epic 5: Frontend Integration (Sprints 36-38)
- **Goal:** Wire TypeScript frontend to Rust
- **Deliverables:** Tauri commands, event streaming, TS hooks, E2E tests
- **Points:** 88

### Epic 6: Ship It (Sprints 39-41)
- **Goal:** Production release
- **Deliverables:** Bug fixes, performance optimization, release builds
- **Points:** 90

## Timeline

| Phase | Sprints | Duration | Start | End |
|-------|---------|----------|-------|-----|
| Foundation | 24-26 | 6 weeks | Now | +6w |
| Tools | 27-29 | 6 weeks | +6w | +12w |
| Agent | 30-32 | 6 weeks | +12w | +18w |
| Backend | 33-35 | 6 weeks | +18w | +24w |
| Integration | 36-38 | 6 weeks | +24w | +30w |
| Ship | 39-41 | 6 weeks | +30w | +36w |

**Total: 36 weeks (9 months)**

## Next Steps

1. **Start Sprint 24**: `epic-1/sprint-24.md`
2. Follow stories in order
3. Complete acceptance criteria before moving to next story
4. Call code-reviewer at end of each sprint

## Success Metrics

### Performance
- Startup: 3s → 0.1s (30x faster)
- Edit latency: 3s → 0.5s (6x faster)
- Memory: 300MB → 50MB (6x less)
- Binary: ~10MB (vs hundreds with Node)

### Quality
- Edit success rate: 70% → 90%
- Recovery rate: 85%
- Context relevance: +30%
- Sandbox startup: 5s → 0.1s (50x faster)

### Coverage
- 35 high-quality tools (down from 55)
- 100% Rust backend
- TypeScript frontend only
- 18 sprints, 51 stories

---

**Ready to start?** Open `epic-1/sprint-24.md` and begin Story 1.1!
