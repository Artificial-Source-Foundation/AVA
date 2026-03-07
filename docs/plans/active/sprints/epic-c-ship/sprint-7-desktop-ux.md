# Sprint 7: Desktop UX Polish

**Epic:** C — Ship
**Duration:** 1 week
**Goal:** The app feels fast, looks good, and streaming works end-to-end
**Parallel with:** Sprint 8 (Plugin Ecosystem)

---

## Competitive Landscape

| Tool | UI | Streaming | Key UX feature |
|---|---|---|---|
| **Zed** | Native editor (Rust/GPUI) | Real-time token + edit streaming | Batch event processing |
| **Cline** | VS Code sidebar | Streaming partial previews | Auto-approval notifications |
| **Continue** | VS Code/JetBrains | 3-process IPC | IDE-agnostic core |
| **AVA** | Tauri + SolidJS (desktop) | Partial | Dev team hierarchy UI |

**Target:** Zed's streaming responsiveness + Cline's approval UX, in our Tauri app.

---

## Story 7.1: Real-Time Token Streaming

**Reference:** `docs/reference-code/zed/crates/agent/src/thread.rs` (line 1723, batch events)

**What Zed does:**
- Batches all immediately-available LLM events in single UI update
- Avoids redundant notifications per token
- Processes tool calls asynchronously without blocking stream

**What to wire:**
Agent 3 built `src/hooks/use-rust-agent.ts` with event listening.
Wire it into the main chat view:

- LLM tokens → render character by character with 16ms debounce
- Tool calls → show tool name + spinner immediately
- Tool results → show in collapsible section
- Progress → show in status bar
- Complete → show summary + modified files list

**Acceptance criteria:**
- [ ] Tokens render in real-time (no batch delay)
- [ ] Tool execution shows progress spinner
- [ ] Streaming feels responsive (<100ms perceived latency)

---

## Story 7.2: Per-Hunk Diff Review UI

**Reference:** `docs/reference-code/zed/crates/agent/src/tools/streaming_edit_file_tool.rs`
**Also:** `docs/reference-code/cline/src/hosts/vscode/VscodeDiffViewProvider.ts`

**What to build in SolidJS:**

`src/components/DiffReview.tsx`:
```tsx
function DiffReview(props: { hunks: Hunk[] }) {
  return (
    <div class="diff-review">
      <For each={props.hunks}>
        {(hunk) => (
          <div class={`hunk ${hunk.status}`}>
            <div class="hunk-header">{hunk.file}:{hunk.startLine}</div>
            <pre class="hunk-removed">{hunk.removed}</pre>
            <pre class="hunk-added">{hunk.added}</pre>
            <div class="hunk-actions">
              <button onClick={() => acceptHunk(hunk.id)}>Accept</button>
              <button onClick={() => rejectHunk(hunk.id)}>Reject</button>
            </div>
          </div>
        )}
      </For>
      <div class="bulk-actions">
        <button onClick={acceptAll}>Accept All</button>
        <button onClick={rejectAll}>Reject All</button>
      </div>
    </div>
  )
}
```

Wire into the diff extension's `diff_review` tool output.

**Acceptance criteria:**
- [ ] Individual hunks can be accepted/rejected
- [ ] Bulk accept/reject all works
- [ ] Diff highlighting (red/green) matches standard diff UX
- [ ] Works with streaming edits from Sprint 3

---

## Story 7.3: Settings & Provider Configuration

**What to build/polish:**

- Model picker with provider grouping (16 providers)
- API key configuration per provider (secure storage via Tauri)
- Permission presets: Strict / Balanced / YOLO
  - Strict: ask for everything
  - Balanced: allow read, ask for write, deny network
  - YOLO: allow all, sandbox bash only
- Theme polish (glass design system from `docs/frontend/design-system.md`)

**Acceptance criteria:**
- [ ] All 16 providers configurable in settings
- [ ] Permission presets work
- [ ] Settings persist across sessions
- [ ] Startup < 500ms (lazy-load extensions)
