# Sprint 10: Docs & Ship

**Epic:** C — Ship
**Duration:** 1 week
**Goal:** Update all docs to reflect v2 architecture, then release

---

## Story 10.1: Update CLAUDE.md

The project root `CLAUDE.md` is the primary reference for AI agents working on this codebase.
It must accurately reflect the post-migration architecture.

**What to update:**

1. **Architecture section** — Replace dual-stack description with hybrid:
   - Rust crates for compute (15 crates, ~3.5K LOC)
   - core-v2 + 18 extensions for orchestration (~15K LOC)
   - Remove all references to `packages/core/` (deleted in Sprint 1)

2. **Project structure** — Update to show:
   ```
   AVA/
   ├── src/                       # Desktop app (Tauri + SolidJS)
   ├── src-tauri/                 # Rust backend + Tauri commands
   ├── crates/                    # Rust compute crates (15)
   ├── packages/
   │   ├── core-v2/               # Agent loop, tools, extensions API (~5K LOC)
   │   ├── extensions/            # 18 built-in extensions (~15K LOC)
   │   ├── platform-node/         # Node.js platform
   │   └── platform-tauri/        # Tauri platform
   └── cli/                       # CLI interface
   ```

3. **Tools table** — Update to ~30 tools (remove dropped tools)

4. **Extensions map** — Update to 18 modules (remove deleted/merged)

5. **How To Add sections** — Verify all examples still work

6. **Quick Commands** — Verify all commands work

7. **Code Style** — Add Rust conventions (already partially there)

8. **Important Notes** — Remove references to migration, dual-stack

**Acceptance criteria:**
- [ ] CLAUDE.md accurately reflects current codebase
- [ ] No references to deleted packages/core/
- [ ] Tool count matches reality
- [ ] Extension count matches reality
- [ ] All example code snippets work

---

## Story 10.2: Update Backend Docs

**Files to update:**

| Doc | What to update |
|---|---|
| `docs/backend.md` | Module index: 18 extensions, Rust crate list |
| `docs/backend/architecture-guide.md` | Hybrid architecture diagram |
| `docs/troubleshooting.md` | Add Rust crate build issues |
| `docs/examples/` | Verify extension examples work with merged extensions |
| `docs/plugins/PLUGIN_SDK.md` | Verify against current ExtensionAPI |
| `docs/frontend/` | Update file map, remove references to old imports |
| `docs/testing-models.md` | Verify model table is current |

**Delete stale docs:**
- `docs/planning/BACKEND-SPRINT-BACKLOG-2026.md` (replaced by REVISED-MIGRATION-PLAN.md)
- `docs/planning/BACKEND-SPRINT-BACKLOG-2026-AGGRESSIVE.md` (superseded)
- `docs/planning/rust-migration-boundaries.md` (superseded)
- `docs/development/rust-backend-epic4-architecture.md` (superseded)

**Acceptance criteria:**
- [ ] All docs reference correct file paths
- [ ] No stale architecture diagrams
- [ ] Plugin SDK examples verified
- [ ] Testing models table current

---

## Story 10.3: Update Memory Files

**Update `/home/xn3/.claude/projects/-home-xn3-Projects-Personal-ASF-Estela/memory/MEMORY.md`:**

- Remove sprint history for old epics 1-6
- Update project structure (no more packages/core/)
- Update current counts (tools, extensions, tests)
- Update key patterns for hybrid architecture
- Add `dispatchCompute()` pattern
- Add Rust crate integration patterns

---

## Story 10.4: Release v2.0

**Checklist:**

1. **Version bump:**
   - `package.json`: `"version": "2.0.0"`
   - `src-tauri/Cargo.toml`: `version = "2.0.0"`
   - All crate `Cargo.toml` versions

2. **CHANGELOG.md:**
   ```markdown
   ## v2.0.0 — Cut & Deepen

   ### Breaking Changes
   - `packages/core/` removed (use `@ava/core-v2` for all imports)
   - Tools reduced from 55 to 30 (see migration guide)
   - Extensions reduced from 37 to 18

   ### Performance
   - Edit tool: 4-tier cascade with 85%+ success rate
   - Streaming edits: <500ms perceived latency (was 3-5s)
   - Startup: <1s (was 3s)
   - Memory: <100MB idle (was 300MB)
   - Rust compute hotpaths: edit, grep, validation, permissions, memory

   ### New Features
   - PageRank repo map (Aider-inspired context ranking)
   - OS-level sandboxing (bwrap/seatbelt, Codex-inspired)
   - Git checkpoints with rollback
   - Dynamic permissions (learn from user decisions)
   - Stuck detection (loop breaking)
   - LLM self-correction on edit failures
   - Per-hunk diff review UI
   - MCP servers as installable plugins
   ```

3. **Cross-platform builds:**
   - `npm run tauri build` for Linux (AppImage, .deb)
   - `npm run tauri build` for macOS (universal .dmg)
   - `npm run tauri build` for Windows (.msi, .exe)

4. **Auto-updater:**
   - Verify update check works
   - Verify update download + install works

**Acceptance criteria:**
- [ ] Version bumped to 2.0.0
- [ ] CHANGELOG written
- [ ] Builds succeed on all 3 platforms
- [ ] Auto-updater works
- [ ] GitHub release created with binaries
