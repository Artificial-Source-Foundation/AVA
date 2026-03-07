# Sprint 1: The Great Deletion

**Epic:** A — Cut & Clean
**Duration:** 1 week
**Goal:** Delete 60K LOC, merge 37 extensions to 18, cut 55 tools to ~30

---

## Story 1.1: Kill packages/core/ (54K lines)

**What to do:**
- Delete `packages/core/src/` entirely (the v1 monolith)
- Create `packages/core/src/index.ts` as a re-export shim pointing to `@ava/core-v2`
- Update all imports across `src/`, `cli/`, and other packages
- Run `pnpm build:all` and fix any broken references

**Reference:** This is unique to AVA — no competitor carries this much legacy code.
Goose started MCP-first with ~1000 lines agent loop. That's the target density.

**Acceptance criteria:**
- [ ] `packages/core/src/` reduced to <100 lines (re-export shim only)
- [ ] All imports point to `@ava/core-v2` or `@ava/extensions`
- [ ] `pnpm build:all` passes
- [ ] `npm run test:run` passes (update broken tests)

---

## Story 1.2: Merge Extensions 37 → 18

**Current → Target mapping:**

| Keep (18) | Absorb into it |
|---|---|
| providers/ | — |
| permissions/ | + rules/ |
| tools-extended/ | + integrations/ (Exa search) |
| prompts/ | — |
| context/ | + codebase/ (repo map, symbols) |
| agent-modes/ | + focus-chain/ (progress tracking) |
| hooks/ | + file-watcher/ + scheduler/ |
| validator/ | — |
| commander/ | — |
| mcp/ | — |
| git/ | — |
| diff/ | — |
| memory/ | — |
| models/ | — |
| plugins/ | — |
| instructions/ | + skills/ + custom-commands/ |
| slash-commands/ | — |
| recall/ | — |
| lsp/ | — |
| server/ | — |

**Delete entirely:**
- `sharing/` — stub, never implemented
- `sandbox/` — Rust crate handles this (`ava-sandbox`)
- `recipes/` — unused, over-engineered
- `profiles/` — low usage, settings covers this
- `github-bot/` — separate service, not desktop app

**How to merge (pattern):**
1. Move source files into target directory
2. Re-export from target's `index.ts`
3. Update `activate()` to register merged functionality
4. Delete empty source directory
5. Update `packages/extensions/index.ts` exports

**Acceptance criteria:**
- [ ] 18 extension directories (down from 37)
- [ ] All merged functionality still works (test each)
- [ ] `packages/extensions/index.ts` exports only 18 modules
- [ ] Zero dead imports

---

## Story 1.3: Cut Tools 55 → ~30

**Keep (30 tools):**

| Category | Tools | Count |
|---|---|---|
| Core | read_file, write_file, edit, bash, glob, grep, pty | 7 |
| Extended | create_file, delete_file, apply_patch, multiedit, ls, batch, question, todoread, todowrite, task, websearch, webfetch, attempt_completion, plan_enter, plan_exit | 15 |
| Git | create_pr, create_branch, switch_branch, read_issue | 4 |
| Memory | memory_read, memory_write, memory_list, memory_delete | 4 |

**Drop (25 tools):**

| Tool | Why drop | Alternative |
|---|---|---|
| codesearch | Redundant | websearch covers it |
| repo_map | Backend only | Rust `ava-codebase` via invoke() |
| bash_background/output/kill | Redundant | pty tool handles this |
| view_image | Not a tool | Inline in chat rendering |
| voice_transcribe | Niche | Plugin |
| inline_suggest | Niche | Plugin |
| edit_benchmark | Dev-only | Not user-facing |
| session_cost | Not a tool | UI widget |
| create_rule | Not a tool | Settings UI |
| create_skill | Not a tool | Plugin CLI |
| diff_review | Keep as UI, not tool | Diff component |
| sandbox_run | Replaced | Rust `ava-sandbox` |
| profile_save/load/list | Low usage | Settings |
| load_skill | Merge | Into instructions extension |
| recall | Keep | (already in list above via memory) |
| 13 delegate_* tools | Simplify | Keep 4 key ones: delegate_coder, delegate_researcher, delegate_reviewer, delegate_explorer |

**Reference:** Goose ships with 4 tools. OpenCode has 24. We're targeting 30 — comprehensive but not bloated.

**Acceptance criteria:**
- [ ] ~30 tools registered (verify with `ava tool list`)
- [ ] Dropped tools removed from tools-extended
- [ ] Delegation tools reduced from 13 to 4
- [ ] All remaining tools have tests
