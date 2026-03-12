# Sprint 66: Optional Capability Backends

## Goal

Add higher-power backend capabilities as opt-in Extended/plugin systems without bloating AVA's 6-tool default surface.

## Backlog Items

| ID | Priority | Name | Outcome |
|----|----------|------|---------|
| B44 | P2 | Web search capability | Add an opt-in Extended web search backend |
| B52 | P2 | AST-aware operations | Add structural code operations as Extended backend capability |
| B53 | P2 | Full LSP exposure to agent | Expand code intelligence as Extended backend capability |
| B69 | P2 | Code search tool | Add richer indexed search as an opt-in capability |

## Why This Sprint

- Delivers powerful backend capabilities without touching the default 6 tools
- Keeps optional power behind Extended/plugin gates
- Builds on the lean-tool policy rather than fighting it

## Scope

### 1. Web search (`B44`)

- Implement as Extended, not core
- Keep provider selection configurable and low-friction

### 2. AST operations (`B52`)

- Add precise structural matching/editing for supported languages
- Start with a narrow, dependable first slice

### 3. LSP exposure (`B53`)

- Add opt-in navigation/intelligence operations gradually
- Keep tool count lean by exposing only what earns its cost

### 4. Rich code search (`B69`)

- Provide stronger search ergonomics over the existing codebase substrate
- Keep this capability optional and composable with indexing work

## Non-Goals

- No default-tool expansion
- No plugin marketplace/distribution UX
- No browser automation in this sprint

## Suggested Execution Order

1. `B44` Web search capability
2. `B52` AST-aware operations
3. `B53` Full LSP exposure
4. `B69` Code search tool

## Verification

- Capability-specific tests for search/AST/LSP behavior
- Config gating tests proving these stay optional
- Manual smoke checks on representative repositories

## Exit Criteria

- Each capability is opt-in and documented as Extended/plugin-first
- No change to the 6-tool default surface
- At least one strong optional backend capability is production-usable
