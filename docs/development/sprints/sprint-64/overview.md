# Sprint 64: Knowledge and Context Foundations

## Goal

Strengthen AVA's backend understanding of projects and workspaces so future editing, planning, and multi-agent features operate on better memory and codebase context.

## Backlog Items

| ID | Priority | Name | Outcome |
|----|----------|------|---------|
| B38 | P2 | Auto-learned project memories | Learn reusable project patterns beyond manual instructions |
| B57 | P2 | Multi-repo context | Understand and search across more than one repository |
| B58 | P3 | Semantic codebase indexing | Add semantic retrieval beyond BM25/PageRank |
| B48 | P2 | Change impact analysis | Estimate blast radius before or alongside edits |

## Why This Sprint

- Improves future agent quality without adding default tools
- Builds a stronger backend substrate for B26, B49, and later multi-agent work
- Shifts AVA from single-repo/local heuristics toward richer project understanding

## Scope

### 1. Project memory learning (`B38`)

- Define a conservative trust/review model for learned memories
- Store useful project-level patterns without spamming memory
- Keep first version local and inspectable

### 2. Multi-repo context (`B57`)

- Support a workspace with multiple repo roots
- Rank and scope retrieval results across repositories
- Keep permissions and path handling explicit

### 3. Semantic indexing (`B58`)

- Add an opt-in semantic retrieval layer on top of existing lexical indexing
- Keep storage/runtime strategy simple in the first version
- Avoid turning this into a mandatory always-on subsystem

### 4. Change impact analysis (`B48`)

- Surface likely affected files/tests/dependencies from existing code intelligence
- Start with conservative, explainable impact summaries
- Reuse index/LSP/codebase primitives where possible

## Non-Goals

- No new default tools
- No marketplace/distribution work
- No TUI-first feature work

## Suggested Execution Order

1. `B38` Auto-learned project memories
2. `B57` Multi-repo context
3. `B58` Semantic codebase indexing
4. `B48` Change impact analysis

## Verification

- Retrieval/index tests for multi-repo and semantic search behavior
- Memory precision tests to avoid noisy auto-learned state
- End-to-end impact analysis checks on representative repos

## Exit Criteria

- AVA can retain and reuse project knowledge more effectively
- Multi-repo retrieval works without breaking single-repo assumptions
- Semantic retrieval is available as an opt-in backend capability
- Impact analysis produces useful, explainable summaries
