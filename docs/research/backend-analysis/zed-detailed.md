# Zed Agent System: Deep Competitive Intelligence Analysis

> A comprehensive analysis of Zed's AI/agent backend architecture, covering every tool, every
> architectural decision, and the reasoning behind each. This goes beyond "what" to explain "why"
> each component exists, what problems it solves, and what would break without it.
>
> Based on direct reading of the Zed source code at `docs/reference-code/zed/`.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture: Why It's Built This Way](#2-architecture-why-its-built-this-way)
3. [The Thread Model: Why Not a Traditional Agent](#3-the-thread-model-why-not-a-traditional-agent)
4. [Tool System: The Engineering Behind 17 Tools](#4-tool-system-the-engineering-behind-17-tools)
5. [Deep Dive: Every Tool and Why It Exists](#5-deep-dive-every-tool-and-why-it-exists)
6. [The Edit Agent: Zed's Most Innovative Subsystem](#6-the-edit-agent-zeds-most-innovative-subsystem)
7. [Permission System: Defense in Depth](#7-permission-system-defense-in-depth)
8. [Turn Execution Loop: Why This Design](#8-turn-execution-loop-why-this-design)
9. [Streaming Architecture: The Speed Advantage](#9-streaming-architecture-the-speed-advantage)
10. [Subagent System: Controlled Parallelism](#10-subagent-system-controlled-parallelism)
11. [What Would Break Without Each Component](#11-what-would-break-without-each-component)
12. [Innovations Worth Stealing](#12-innovations-worth-stealing)
13. [Weaknesses and Gaps](#13-weaknesses-and-gaps)
14. [Comparison to AVA](#14-comparison-to-ava)

---

## 1. Executive Summary

Zed's agent system is a **deeply editor-integrated** AI coding assistant built in Rust. It is NOT a
standalone agent framework -- it is an agent that lives inside a code editor, which gives it unique
advantages that standalone CLI tools cannot replicate:

- **Direct buffer manipulation** -- edits happen in the editor's buffer system, not via file I/O
- **LSP integration** -- real diagnostics from real language servers, not file-level heuristics
- **Real-time streaming edits** -- users see characters appear as the LLM generates them
- **Visual diff review** -- multibuffer diff pane for reviewing all changes at once
- **Inline assistant** -- separate AI pipeline for in-place code editing

### Key Numbers

| Metric | Value |
|--------|-------|
| Built-in tools | 17 (+ MCP dynamic tools) |
| LLM providers | 14 native + OpenAI-compatible |
| Core agent file | ~4,144 lines (`thread.rs`) |
| Edit agent subsystem | ~3,051 lines across 4 files |
| Permission system | ~1,191 lines across 2 files |
| Crates involved | ~25 agent-related crates |

### The One-Sentence Summary

Zed's agent is optimized for **real-time collaborative editing with an AI** -- every design
decision prioritizes the experience of watching an AI edit your code live, reviewing those edits
granularly, and maintaining full control over what gets written to disk.

---

## 2. Architecture: Why It's Built This Way

### The Editor-Native Advantage

Most AI coding agents (Claude Code, Aider, OpenCode, Goose) operate on **files**. They read files
from disk, generate edits, and write files back. Zed operates on **buffers** -- the in-memory
representations of files that the editor maintains.

**Why this matters:**

1. **No write-read-write cycles.** When the agent edits a file, the change appears instantly in the
   editor. No need to save, re-read, or refresh. The buffer IS the source of truth.

2. **Conflict detection is free.** The editor tracks buffer versions (mtime). If a user edits a
   file while the agent is working, the agent detects this immediately via buffer state, not by
   checking file timestamps on disk.

3. **Undo is native.** Editor undo/redo works on agent changes because they're regular buffer
   operations. No custom rollback system needed (though Zed also has action logging).

4. **Format-on-save integration.** When the agent saves a file, the editor's format-on-save
   pipeline runs automatically. The agent doesn't need to know about formatters.

### The GPUI Entity System

Everything in Zed is a GPUI entity -- `Entity<Thread>`, `Entity<Project>`, `Entity<Buffer>`. This
is Zed's custom reactive UI framework where entities are reference-counted, observable, and can be
mutated through typed contexts (`Context<T>`).

**Why this matters for the agent:**

- **Automatic UI updates.** When `Thread` state changes (new message, tool call starts, edit
  applied), the UI reacts automatically through GPUI's observation system. No manual event bus.
- **Lifetime management.** Entity references are weak-referenceable, so the agent can hold
  references to buffers without preventing garbage collection.
- **Async safety.** GPUI provides `AsyncApp` for async code, ensuring UI mutations happen on the
  main thread while computation happens on background threads.

### The ACP Separation

Zed introduces ACP (Agent Client Protocol) as an abstraction layer between the agent logic and the
UI. There are two thread types:

- `Thread` (in `thread.rs`) -- the actual agent logic: turn loop, tool dispatch, LLM calls
- `AcpThread` -- the protocol layer that communicates with the UI

**Why two thread types?**

This separation exists to support **remote agents**. The ACP protocol means the agent logic could
run on a remote server while the UI runs locally. Today this is used for Zed's cloud agent service,
but the architecture supports any remote execution scenario.

**What would break without it:** The agent would be permanently coupled to the local editor process.
No remote agent execution, no agent-as-a-service, no headless mode.

---

## 3. The Thread Model: Why Not a Traditional Agent

### Threads, Not Agents

Zed's core unit is a `Thread` (conversation thread), not an "Agent" object. The `NativeAgent`
struct exists but is primarily a session manager -- it creates and manages `Thread` entities.

**Why threads instead of agents:**

1. **Persistence model.** Threads are conversations that can be saved, loaded, and resumed. An
   "agent" implies a running process, but a "thread" implies data that can be serialized.

2. **Multiple sessions.** A user can have many thread tabs open simultaneously. Each is an
   independent conversation with its own message history, model selection, and tool state.

3. **Subagent spawning.** When the `spawn_agent` tool creates a child, it creates a new `Thread`
   entity. This is clean -- it's the same type, just with a `SubagentContext` attached.

### The Message Model

```
Message::User(UserMessage)     -- user text + @mention context
Message::Agent(AgentMessage)   -- LLM response + tool results + reasoning
Message::Resume                -- "continue where you left off" marker
```

The `Resume` message type is notable. It exists because LLMs have output token limits. When the
model hits its limit, the user can send a Resume to continue. This is a first-class concept, not a
hack.

### Thread State Machine

```
Idle → Running Turn → (Tool Execution) → Running Turn → ... → Idle
                ↑              |
                └──── retry ───┘
```

The thread maintains a `running_turn: Option<RunningTurn>` which tracks:
- The async task handle (for cancellation)
- A cancellation watch channel
- The current turn's tool futures

**Why an explicit state machine?** Without it, you'd have race conditions between:
- User sending a new message while the agent is still processing
- User cancelling while tools are running
- Subagent spawning while the parent is between turns

---

## 4. Tool System: The Engineering Behind 17 Tools

### The `AgentTool` Trait

Every tool implements:

```rust
trait AgentTool: 'static + Sized {
    type Input: Deserialize + Serialize + JsonSchema;
    type Output: Deserialize + Serialize + Into<LanguageModelToolResultContent>;

    const NAME: &'static str;

    fn kind() -> acp::ToolKind;           // Read, Write, Execute, Fetch, Agent
    fn run(self: Arc<Self>, input: ToolInput<Self::Input>, event_stream: ToolCallEventStream, cx: &mut App)
        -> Task<Result<Self::Output, Self::Output>>;
    fn replay(&self, input: Self::Input, output: Self::Output, ...) -> Result<()>;
    fn initial_title(&self, ...) -> SharedString;
    fn input_schema(format: LanguageModelToolSchemaFormat) -> Schema;
    fn supports_input_streaming() -> bool;
    fn supports_provider(provider: &LanguageModelProviderId) -> bool;
}
```

### Design Decision: `Result<Output, Output>`

Tools return `Result<Self::Output, Self::Output>` -- NOT `Result<Output, anyhow::Error>`.

**Why this is clever:**

When a tool fails, the error message goes back to the LLM as a tool result. If errors were
`anyhow::Error` (arbitrary strings), the LLM would receive unstructured error messages. By forcing
errors to be the same `Output` type, tool authors must structure their error messages the same way
they structure their success messages.

**What would break without it:** Error messages to the LLM would be inconsistent. Some tools would
return stack traces, others would return user-friendly messages. The LLM's ability to recover from
tool errors would be unreliable.

### Design Decision: Compile-Time Name Validation

The `tools!` macro checks at compile time that no two tools share the same `NAME`:

```rust
tools!(
    TerminalTool,
    EditFileTool,       // or StreamingEditFileTool behind feature flag
    ReadFileTool,
    // ... all tools listed
);
```

**Why this matters:** In most agent frameworks, duplicate tool names are a runtime error (or worse,
silently override each other). Zed catches this at compile time. This is only possible because
tools are registered statically in Rust, not dynamically at runtime.

**What would break without it:** A developer could accidentally register two tools with the same
name, and the LLM would get confused about which tool to call. This would be extremely hard to
debug because the symptom is "the LLM sometimes does the wrong thing."

### Design Decision: Tool Kinds

Each tool declares its `kind()`: `Read`, `Write`, `Execute`, `Fetch`, or `Agent`.

**Why kinds exist:**

1. **Permission grouping.** The permission system supports `ByCategory` mode where users can
   approve all "Read" tools at once.
2. **UI organization.** The agent profile UI groups tools by kind.
3. **Security escalation.** Write/Execute tools get stricter default permissions than Read tools.
4. **Model context.** The LLM can be told "this is a read-only operation" vs "this will modify
   files."

### Design Decision: Input Streaming

Some tools support receiving partial input as the LLM streams JSON:

```rust
fn supports_input_streaming() -> bool { true }  // for streaming_edit_file

// ToolInput<T> provides:
async fn recv_partial(&mut self) -> Option<Result<T>>  // partial JSON so far
async fn recv(&mut self) -> Result<T>                   // final complete JSON
```

This is backed by an mpsc channel where the partial JSON fixer sends increasingly complete
versions of the input as more tokens arrive from the LLM.

**Why this is revolutionary:** Without input streaming, the agent must wait for the LLM to finish
generating the ENTIRE tool call JSON before it can start doing anything. For large edits (replacing
100 lines of code), this means seconds of dead time. With streaming, the tool can start applying
edits while the LLM is still generating the rest.

**What would break without it:** The streaming edit tool would be impossible. Every edit would have
a noticeable delay between the LLM finishing generation and the edit appearing in the buffer. The
"watching AI type in real time" experience that makes Zed unique would not exist.

### Design Decision: `ToolCallEventStream`

Every tool receives a `ToolCallEventStream` that provides:
- **Title updates** -- change the displayed title as the tool progresses
- **Location updates** -- show which file/line the tool is working on
- **Diff previews** -- stream edit previews before they're applied
- **User cancellation** -- detect when the user clicks "stop"
- **Authorization prompts** -- ask user permission for dangerous operations

**Why every tool gets one:** This creates a consistent UX across all tools. The user always knows
what the agent is doing, where it's working, and can always cancel. Without it, some tools would
be black boxes (especially long-running ones like `terminal`).

### Design Decision: `replay()`

Every tool has a `replay()` method that re-applies a tool's effects from saved input/output.

**Why this exists:** Thread persistence. When a user loads a saved conversation, the UI needs to
reconstruct the state that tools created (opened files, applied edits, etc.). `replay()` lets
each tool handle its own reconstruction without re-running the actual operation.

**What would break without it:** Loading a saved conversation would show tool calls as empty
boxes with no context. The user couldn't see what edits were made, what files were read, etc.

---

## 5. Deep Dive: Every Tool and Why It Exists

### 5.1 `terminal` — Shell Command Execution

**The problem it solves:** The agent needs to run arbitrary commands -- tests, builds, linters,
git operations, package managers.

**Implementation details:**
- Commands run in a real shell (user's default shell from settings)
- Working directory validated against project worktrees (cannot escape project root)
- Output truncated to 16KB to avoid blowing up context windows
- Timeout support to prevent runaway commands
- Detects when the user manually stops a command vs. when it times out

**Why 16KB output limit:** LLM context windows are precious. A `cargo build` on a large project
can produce megabytes of output. 16KB captures enough error context without wasting tokens.

**Why working directory validation:** Without it, the agent could `cd /etc && rm -rf *`. The
worktree check ensures commands run within the project boundary.

**What would break without this tool:** The agent couldn't run tests, build projects, install
dependencies, or use any CLI tool. It would be limited to reading and writing files with no way
to verify its changes work.

### 5.2 `edit_file` — LLM-Delegated File Editing

**The problem it solves:** The agent needs to modify existing files, but having the primary LLM
generate exact text replacements is unreliable (it hallucinates whitespace, gets indentation
wrong, misses context).

**The clever solution:** Instead of having the primary LLM generate edit operations directly,
`edit_file` delegates to a **secondary LLM call** via the `EditAgent`. The primary LLM describes
WHAT to edit in natural language, and the EditAgent figures out HOW.

**Implementation details:**
- Reads the current buffer content (not the file on disk -- buffer may have unsaved changes)
- Tracks buffer mtime to detect if the file was modified externally since last read
- Sends the file content + edit description to the EditAgent
- EditAgent uses a specialized prompt that outputs search/replace blocks
- Supports file creation (write full content) and overwrite (replace entire file)
- Respects format-on-save settings -- after editing, the buffer is formatted automatically
- Detects unsaved user changes and warns the model

**Why a secondary LLM call:** The primary LLM is having a conversation and making high-level
decisions. Asking it to ALSO produce exact text matches against a file it saw 10 messages ago is
unreliable. The EditAgent sees the CURRENT file content and focuses solely on producing correct
edits. This separation of concerns dramatically improves edit accuracy.

**Why mtime tracking:** Without it, the agent could overwrite user changes. Scenario: user edits
line 50, agent (which last read the file 3 turns ago) also edits line 50. With mtime tracking,
the edit_file tool detects the mismatch and can re-read the file first.

**What would break without this tool:** File editing would be unreliable. The primary LLM would
need to output exact text matches, which fails frequently with indentation, whitespace, and
context drift. Edit accuracy would drop significantly.

### 5.3 `streaming_edit_file` — Real-Time Streaming Edits

**The problem it solves:** The `edit_file` tool has latency -- it must wait for the EditAgent's
entire response before applying changes. Users want to see edits happen in real time.

**The clever solution:** This tool processes the LLM's JSON output AS IT STREAMS. It receives
partial `old_text`/`new_text` pairs and applies them to the buffer character-by-character.

**Implementation details:**
- Feature-flagged (can replace `edit_file` transparently -- same tool name to the LLM)
- Uses `StreamingFuzzyMatcher` to find where `old_text` matches in the buffer
- Uses `StreamingDiff` for character-level diff computation
- Auto-reindents replacements to match surrounding code
- Processes multiple edit operations in sequence within a single tool call

**Why fuzzy matching:** The LLM's `old_text` is not guaranteed to exactly match the buffer. It
might have slightly different whitespace, or the buffer may have been reformatted. The fuzzy
matcher uses dynamic programming with asymmetric costs (deletion=10, insertion=3, replacement=1)
to find the best match. The 80% match ratio threshold allows significant deviations while
preventing false matches.

**Why asymmetric costs:** Deletions (content in the buffer that the LLM "forgot") are expensive
because they indicate the LLM is matching the wrong location. Insertions (content the LLM added
that isn't in the buffer) are cheap because the LLM might be including surrounding context.
Replacements are cheapest because they usually represent whitespace or formatting differences.

**Why auto-reindentation:** When the LLM generates replacement code, it uses whatever indentation
it "remembers" from the file. But the actual file might use different indentation. The reindenter
computes the indent delta between the buffer and the LLM's output, then adjusts every line.

**What would break without this tool:** The "watching AI type" experience disappears. Edits would
appear as instant bulk replacements instead of streaming character-by-character. This is Zed's
primary UX differentiator and without it, the editing experience is no different from a CLI tool.

### 5.4 `read_file` — Smart File Reading

**The problem it solves:** The agent needs to read file contents, but files can be very large, and
reading entire large files wastes context window tokens.

**Implementation details:**
- For files under 16KB: returns full content
- For files over 16KB: returns an **outline** (function signatures, class definitions, etc.)
  generated by Tree-sitter parsing
- Supports line range reading (`start_line` to `end_line`)
- Reads images and returns them as image content (for vision-capable models)
- Tracks mtime on every read (used by `edit_file` for conflict detection)
- Respects `file_scan_exclusions` (e.g., `node_modules/`) and `private_files` settings
- Detects symlink escapes (file outside project accessed via symlink)

**Why the outline threshold:** A 100KB JavaScript file would consume ~25K tokens. The outline
gives the agent the file's structure (function names, class hierarchy, exports) in ~2K tokens.
The agent can then use line ranges to read specific sections it needs.

**Why mtime tracking:** This is the foundation of edit safety. Every `read_file` records the
buffer's modification timestamp. When `edit_file` runs later, it checks if the mtime has changed.
If it has, the file was modified (by the user or another tool) and the edit might be based on
stale content.

**Why symlink escape detection:** Without it, a symlink from `project/link` -> `/etc/passwd`
would let the agent read arbitrary system files through what appears to be a project-relative
path.

**What would break without this tool:** The agent would be blind -- unable to read any code.
Without the outline mode, it would either waste tokens on large files or need a separate "outline"
tool, fragmenting the reading experience.

### 5.5 `find_path` — Glob-Based File Search

**The problem it solves:** The agent needs to find files by name pattern, like "find all test
files" or "find the configuration file."

**Implementation details:**
- Uses glob patterns (e.g., `**/*.test.ts`, `**/config.*`)
- Paginated results (50 per page) to avoid blowing up context
- Runs on a background thread against worktree snapshots (non-blocking)
- Returns relative paths from the worktree root

**Why worktree snapshots:** Zed's worktree system maintains an in-memory snapshot of the file
tree. Searching this snapshot is instant -- no filesystem I/O. This is dramatically faster than
shelling out to `find` or `fd`, especially on large projects.

**Why pagination:** A glob like `**/*.ts` on a large TypeScript project could return thousands
of files. Sending all of them to the LLM would waste context. Pagination forces the agent to
be specific or iterate.

**What would break without this tool:** The agent would need to use `terminal` with `find` or
`ls -R` commands, which are slower (filesystem I/O) and produce unstructured output the LLM
must parse. File discovery would be unreliable.

### 5.6 `grep` — Regex Search with AST Context

**The problem it solves:** The agent needs to find code patterns across the project -- function
calls, string literals, error messages, etc.

**Implementation details:**
- Full regex support with optional case sensitivity
- Results are paginated (20 per page)
- **AST-aware context:** Each match includes its parent syntax node (e.g., the function
  containing the matching line), not just the matching line
- Respects file exclusions and private files
- Runs against worktree snapshots

**Why AST-aware context:** This is a key differentiator. When you search for `handleError`, a
regular grep shows you the matching line. Zed's grep shows you the FUNCTION containing that
line. This gives the agent the context it needs to understand HOW the match is used, not just
WHERE it is.

**Why 20 results per page (not 50 like find_path):** Search results include context (the
surrounding syntax node), so each result is much larger than a file path. 20 results with
context uses roughly the same token budget as 50 bare paths.

**What would break without this tool:** The agent would use `terminal` with `grep` or `rg`,
getting raw line matches without semantic context. It would need extra `read_file` calls to
understand each match, wasting turns and tokens.

### 5.7 `diagnostics` — LSP Error/Warning Reader

**The problem it solves:** The agent needs to know if its changes introduced errors. Without
real diagnostics, it can only run the compiler via terminal and parse output.

**Implementation details:**
- When given a file path: returns all errors and warnings for that file
- When given no path: returns a project-wide summary of all diagnostic errors
- Data comes from Zed's LSP integration -- real language server diagnostics, not heuristics
- Includes diagnostic message, severity, and location

**Why this is a killer feature:** No CLI-based agent has this. When Zed's agent edits a TypeScript
file, the TypeScript language server immediately reports type errors. The agent can read these via
`diagnostics` without running `tsc`. This is faster (LSP is already running) and more accurate
(same diagnostics the user sees).

**What would break without this tool:** The agent would need to run the compiler/linter via
`terminal` after every edit to check for errors. This is slow (full compilation vs. incremental
LSP) and produces unstructured output. The agent would also miss warnings that the LSP catches
but the compiler might not surface prominently.

### 5.8 `web_search` — Web Search

**The problem it solves:** The agent needs to look up documentation, error messages, or API
references that aren't in the project.

**Implementation details:**
- Routes through Zed's cloud provider (not direct API calls)
- Returns structured results: title, URL, snippet
- Permission-gated (user must approve web access)

**Why Zed Cloud only:** Web search requires API keys (Google, Bing, etc.). Rather than asking
users to configure search API keys, Zed proxies through their cloud service. This simplifies
setup at the cost of requiring a Zed account.

**What would break without it:** The agent would need to use `fetch` with known URLs, or ask the
user for help. It couldn't independently research unfamiliar APIs or debug obscure errors.

### 5.9 `fetch` — URL Fetching with Smart Conversion

**The problem it solves:** The agent needs to read web pages -- documentation, API references,
Stack Overflow answers.

**Implementation details:**
- HTML pages are converted to Markdown (readable by the LLM)
- Special handler for Wikipedia (cleaner extraction)
- JSON responses are pretty-printed
- Plain text is passed through
- Permission-gated with URL pattern matching

**Why HTML-to-Markdown:** Raw HTML is full of tags, scripts, and navigation elements that waste
tokens. Converting to Markdown strips the noise and gives the LLM clean, readable content.

**Why URL pattern permissions:** Instead of approving every URL individually, users can approve
patterns like `*.docs.rs` or `*.mozilla.org`. This balances security with usability.

**What would break without it:** The agent couldn't read documentation or web resources. Combined
with no `web_search`, the agent would be completely unable to look up anything external.

### 5.10 `spawn_agent` — Subagent Creation

**The problem it solves:** Some tasks benefit from parallel execution or isolation. A main agent
working on feature A might need a separate context for feature B.

**Implementation details:**
- Creates a new `Thread` entity with `SubagentContext { parent_thread_id, depth }`
- Subagents do NOT see the parent's conversation history (clean context)
- Supports follow-up messages via `session_id` parameter
- Max depth of 1 (subagents cannot spawn sub-subagents)
- Parent model settings (model, thinking, speed) propagate to subagents
- Returns only the final message to the parent
- Rate limit semaphore released before tool execution to prevent deadlocks

**Why max depth 1:** Unlimited nesting would create exponential resource usage and make
debugging impossible. Depth 1 gives the benefits of parallelism without the complexity of
recursive agent trees.

**Why no shared history:** The subagent gets a focused context. If it inherited the parent's
entire conversation (which might be 50+ messages about feature A), it would be confused about
its task. Clean context means the subagent prompt is the only context, leading to more focused
execution.

**Why rate limit semaphore release:** The parent thread holds a rate limit semaphore while
processing a turn. If subagents also need the semaphore, they'd deadlock (parent holds it,
waiting for subagent, which is waiting for the semaphore). Releasing before tool execution
prevents this.

**What would break without it:** The agent would be single-threaded -- no parallel execution,
no task isolation. Complex multi-file tasks would take longer because they'd be sequential.

### 5.11 `save_file` — Batch File Saving

**The problem it solves:** After the agent edits buffers, changes exist only in memory. They
need to be written to disk for builds, tests, and git operations to see them.

**Implementation details:**
- Accepts multiple file paths for batch saving
- Tracks dirty/clean status per buffer
- Symlink escape checks on every path
- Sensitive settings detection (prevents saving to `.zed/` config or global settings)
- Per-file error reporting (one file failing doesn't prevent others from saving)
- Triggers format-on-save pipeline

**Why batch saving:** The agent often edits 5-10 files in a single turn. Without batch saving,
it would need 5-10 separate `save_file` calls, each consuming a tool use turn. Batch saves are
a single tool call.

**Why sensitive settings detection:** The `.zed/` directory contains editor settings, key
bindings, and tool permissions. If the agent could write to these, it could disable its own
permission system. This is a critical security boundary.

**What would break without it:** Buffer edits would never reach disk. The agent could edit code
all day, but tests would see the old versions. Alternatively, the agent would need to use
`terminal` with shell commands to write files, bypassing the buffer system entirely.

### 5.12 `restore_file_from_disk` — Buffer Reload

**The problem it solves:** Sometimes the agent (or a tool like `terminal` running a formatter)
modifies a file on disk, but the editor buffer still shows the old content.

**Implementation details:**
- Accepts multiple file paths for batch reload
- Discards any unsaved buffer changes
- Same security model as `save_file` (symlink escape, sensitive settings)
- Per-file error reporting

**Why this exists as a separate tool:** This is the inverse of `save_file`. Where `save_file`
pushes buffer -> disk, `restore_file_from_disk` pulls disk -> buffer. Both directions are needed
because the buffer and disk can get out of sync in both directions.

**What would break without it:** After running `npx prettier --write .` via terminal, the
editor buffers would show pre-formatted code while the disk has formatted code. The agent's
subsequent `read_file` calls would return stale buffer content, leading to confusion and
incorrect edits.

### 5.13 `list_directory` — Directory Listing

**The problem it solves:** The agent needs to understand project structure -- what files are in
a directory, how folders are organized.

**Implementation details:**
- Separates output into folders and files
- Respects `file_scan_exclusions` at both global and worktree levels
- Handles `private_files` settings
- Gracefully handles "." input (current directory) and glob-like inputs
- Uses worktree snapshots (instant, no filesystem I/O)

**Why separate from `find_path`:** `find_path` searches recursively by pattern. `list_directory`
shows one level of a directory. They serve different cognitive needs: "find all test files" vs.
"what's in the src/ directory?"

**What would break without it:** The agent would use `terminal` with `ls`, which doesn't respect
editor exclusion settings and produces unstructured output. Or it would use `find_path` with
`src/*`, which works but is semantically wrong (pattern search vs. directory listing).

### 5.14 `create_directory` — Directory Creation

**The problem it solves:** When the agent creates new features, it often needs new directories
(e.g., `src/components/new-feature/`).

**Implementation details:**
- Creates parent directories automatically
- Symlink escape authorization
- Sensitive settings detection

**Why this isn't just `terminal` with `mkdir -p`:** Security. The tool checks for symlink
escapes and sensitive paths. A `mkdir -p` command in the terminal would bypass these checks.

**What would break without it:** The agent would use `terminal` for directory creation, bypassing
security checks. Or directory creation would fail silently when the agent tries to create a file
in a non-existent directory.

### 5.15 `delete_path` — Recursive Deletion

**The problem it solves:** The agent needs to remove files or directories during refactoring.

**Implementation details:**
- Recursive deletion for directories
- **ActionLog integration** -- records what was deleted and which agent session did it
- Symlink escape authorization
- Sensitive settings detection

**Why ActionLog integration:** This is unique. When the agent deletes a file, the action is
recorded in the ActionLog with attribution to the specific agent session. This enables:
- Reviewing what the agent deleted
- Potentially undoing deletions
- Auditing agent behavior

**What would break without it:** File deletion would be untracked. If the agent deletes the
wrong file, there's no record of what happened or which session did it.

### 5.16 `move_path` / `copy_path` — File Operations

**The problem it solves:** Refactoring often involves moving or copying files.

**Implementation details:**
- Both source and destination paths checked for symlink escapes
- Uses `collect_symlink_escapes` to batch-check both paths
- Same security model as other file tools

**Why dual-path symlink checking:** A move from `project/link` -> `project/real/` where `link`
is a symlink to `/etc/` would let the agent move system files. Both paths must be validated.

**What would break without these tools:** The agent would use `terminal` with `mv`/`cp`,
bypassing all security checks. File operations would be unvalidated.

### 5.17 `now` — Current Time

**The problem it solves:** LLMs don't know what time it is. The agent might need the current
date for file headers, commit messages, or time-based logic.

**What would break without it:** The agent would use `terminal` with `date`, which works but
wastes a tool call on something trivial.

### 5.18 `open` — Open in Editor

**The problem it solves:** After making changes, the agent might want to show the user a
specific file.

**What would break without it:** The agent would tell the user "please open file X" instead of
opening it directly. Minor UX issue but it breaks the flow of autonomous operation.

---

## 6. The Edit Agent: Zed's Most Innovative Subsystem

The EditAgent is a **sub-agent within the agent** -- a secondary LLM call specifically for
applying file edits. This is Zed's most sophisticated and innovative component.

### Why a Separate Edit Agent?

The fundamental problem: LLMs are bad at producing exact text matches.

When you tell an LLM "replace the function `foo` with this new version," it needs to output:
1. The exact text to find in the file (the "old text")
2. The exact text to replace it with (the "new text")

The primary LLM often gets this wrong because:
- It last saw the file 10+ messages ago
- It doesn't track whitespace precisely
- It confuses similar code blocks in the same file
- It might be working from an outline, not the full file

**Zed's solution:** The primary LLM describes the edit in natural language ("add error handling
to the `process` function"). The EditAgent receives the CURRENT file content plus this
description and produces the actual search/replace operations.

### Two Edit Formats

The EditAgent supports two output formats, selected per-model:

**XML Tags Format** (default for most models):
```xml
<old_text>
function foo() {
    return 1;
}
</old_text>
<new_text>
function foo() {
    return 2;
}
</new_text>
```

**DiffFenced Format** (used for Gemini):
```
<<<<<<< SEARCH
function foo() {
    return 1;
}
=======
function foo() {
    return 2;
}
>>>>>>> REPLACE
```

**Why two formats:** Different models have different strengths with different output formats.
Gemini performs better with diff-fenced format (similar to git conflict markers it's seen in
training data). Anthropic/OpenAI models perform better with XML tags.

**LLM Quirk Handling:** The XML parser handles Anthropic Sonnet's known quirk of sometimes
emitting `</parameter></invoke>` tags (from its training on XML tool-use examples). The parser
recognizes and strips these.

### The Streaming Fuzzy Matcher (802 lines)

This is the core innovation that makes streaming edits possible.

**The problem:** The EditAgent generates `old_text` blocks that need to be found in the buffer.
But the text might not match exactly (whitespace differences, formatting, minor edits since
the file was read).

**The solution:** A streaming dynamic programming algorithm that computes edit distance
incrementally as chunks of the query arrive.

**Key parameters:**
- **Edit distance matrix:** Standard DP matrix computed incrementally
- **Asymmetric costs:** deletion=10, insertion=3, replacement=1
- **Match threshold:** 80% match ratio (allows 20% deviation)
- **Line hint tolerance:** 200 lines (if a hint says "near line 50", search lines 1-250)

**Why asymmetric costs are brilliant:**

| Cost Type | Meaning | Cost | Rationale |
|-----------|---------|------|-----------|
| Deletion | Buffer has text LLM "forgot" | 10 | High: LLM probably matched wrong location |
| Insertion | LLM added text not in buffer | 3 | Low: LLM might include context around the match |
| Replacement | Single character difference | 1 | Lowest: whitespace/formatting differences |

This means the matcher strongly prefers locations where the LLM's text is a superset of the
buffer text (insertions are cheap) over locations where the buffer has text the LLM missed
(deletions are expensive). This matches the typical failure mode: LLMs add context they
remember, not remove text they forgot.

**Disambiguation with line hints:** When the same code pattern appears multiple times (e.g.,
`return null;` on multiple lines), the line hint narrows the search window. 200-line tolerance
handles LLMs that give approximate line numbers.

### The Streaming Diff Algorithm

Once the matcher finds WHERE to edit, the `StreamingDiff` computes HOW to transform the old
text into the new text, character by character:

1. Receives new characters as they stream from the LLM
2. Maintains a DP matrix for character-level edit distance
3. Emits `Insert` and `Delete` operations
4. Groups character operations into line operations
5. Applies to the buffer in real time

**Why character-level:** Line-level diffs would cause entire lines to flicker (delete + insert)
even for single-character changes. Character-level diffs produce smooth, minimal edits.

### The Reindenter (214 lines)

After the fuzzy matcher and streaming diff, the edited text might have wrong indentation.

**The algorithm:**
1. Compare the indentation of the first line in the buffer vs. the first line in the LLM output
2. Compute the delta (e.g., buffer has 4 spaces, LLM output has 2 spaces → delta is +2)
3. Apply the delta to every subsequent line
4. Handle mixed tabs/spaces

**Why streaming:** The reindenter processes lines as they arrive, not as a batch at the end.
This means indentation is correct from the first visible character, not fixed up after the fact.

### The Tool Edit Parser (941 lines)

This parser converts incrementally-growing partial JSON from LLM streaming into edit events.

**The problem:** The LLM streams JSON character by character. The tool call's input is a JSON
object with `old_text` and `new_text` fields. We need to extract these fields before the JSON
is complete.

**The solution:** A streaming JSON parser that:
1. Tracks the current parse state within the JSON structure
2. Emits `OldTextChunk` events as characters of `old_text` arrive
3. Emits `NewTextChunk` events as characters of `new_text` arrive
4. Handles partial-json-fixer artifacts (trailing backslashes from incomplete escape sequences)
5. Tracks deltas (only emits new characters, not the full accumulated string)

**Why delta tracking matters:** Without it, each partial update would re-send the entire
accumulated string. For a 100-line edit, this means the last update would contain 100 lines
even though only 1 new character was added. Delta tracking ensures each event contains only
the new characters.

---

## 7. Permission System: Defense in Depth

### Architecture (837 + 354 = 1,191 lines)

The permission system has multiple layers:

**Layer 1: Tool Kind Permissions**
- Each tool has a `kind()` (Read/Write/Execute/Fetch/Agent)
- Users can set permission mode: `ByTool`, `ByCategory`, or `AllowAll`
- `ByCategory` means approving all "Write" tools at once

**Layer 2: Pattern-Based Rules**
- After approving a tool, users can choose "Always Allow" with a pattern
- Patterns are extracted from tool inputs:
  - Terminal: `^cargo\s+build(\s|$)` (allows `cargo build` but not `cargo build && rm -rf /`)
  - File paths: parent directory patterns (allow all operations in `src/`)
  - URLs: domain-based patterns (allow all fetches from `docs.rs`)
  - Copy/move: common parent directory of source and destination

**Layer 3: Symlink Escape Detection**
- Every file operation checks if the resolved path escapes the project boundary
- Uses worktree snapshot metadata (avoids blocking filesystem I/O)
- `ResolvedProjectPath` enum: `Safe(PathBuf)` vs `SymlinkEscape(PathBuf)`
- Symlink escapes get a SEPARATE authorization prompt (not the normal tool permission)

**Layer 4: Sensitive Settings Detection**
- Operations on `.zed/` settings directory get special treatment
- Operations on global config directory are flagged
- These are flagged EVEN IF the user has "always allow" for the tool

### Why This Complexity?

**Terminal pattern extraction is the cleverest part.** The regex `^cargo\s+build(\s|$)` allows:
- `cargo build`
- `cargo build --release`
- `cargo build --target wasm32`

But NOT:
- `cargo build && rm -rf /` (the `(\s|$)` anchor prevents chaining)
- `cargo build; malicious command` (same reason)

The system also rejects path-like commands from being "always allowed" because paths are too
specific to be useful patterns.

**Why symlink escapes are a separate authorization:** A symlink escape is fundamentally different
from a normal file operation. It means the agent is accessing files OUTSIDE the project. This
deserves a different, more prominent warning than "the agent wants to write a file."

**What would break without the permission system:** The agent would have unrestricted access to
the filesystem, network, and shell. A malicious prompt injection could exfiltrate data, delete
files, or install malware. The layered approach means even if one layer is bypassed, others
still protect the user.

---

## 8. Turn Execution Loop: Why This Design

### The Loop Structure

```
run_turn_internal:
    loop {
        1. Build completion request (system prompt + messages + tools)
        2. Acquire rate limit semaphore
        3. Stream LLM response
        4. Batch events for efficiency
        5. Process events (text, thinking, tool calls)
        6. Release rate limit semaphore
        7. Execute tools in parallel (FuturesUnordered)
        8. If tools were called → collect results → continue loop
        9. If EndTurn or MaxTokens → break
        10. If error → retry with strategy
    }
```

### Key Design Decisions

**Event batching:** Instead of processing each streaming event individually, the loop collects
all immediately-available events into a batch, then processes the batch. This reduces the
overhead of UI updates (one batch update vs. N individual updates).

**Rate limit semaphore release BEFORE tool execution:** This is subtle but critical. The
semaphore prevents too many concurrent LLM calls. But if the parent agent holds the semaphore
while waiting for a subagent (which also needs the semaphore), deadlock occurs. Releasing before
tools run prevents this.

**Cancellation via watch channels:** A `tokio::sync::watch` channel broadcasts cancellation to
all running tools and subagents. This is more reliable than task cancellation because tools can
check for cancellation at appropriate points (e.g., after saving a file but before starting the
next file).

**Retry strategy selection by error type:**
| Error | Strategy | Rationale |
|-------|----------|-----------|
| 429 Rate Limit | Exponential backoff, 4 attempts | Server will recover, just need to wait |
| 503 Overloaded | Exponential backoff, 4 attempts | Same as rate limit |
| 500 Server Error | Fixed delay, 3 attempts | Might be transient |
| 401/403 Auth | No retry | Won't fix itself |
| 413 Payload Too Large | No retry | Need to reduce context |

### Build Completion Request

The request construction is notable:

1. **System prompt** from Handlebars template (includes project context, OS, shell, available tools)
2. **Tool definitions** filtered by agent profile and model capability
3. **Messages** with user context (mentions, selections, diffs)
4. **Cache marker** on the last message (for Anthropic prompt caching)
5. **Thinking configuration** if enabled

**Why Handlebars for system prompts:** String concatenation for system prompts is error-prone and
hard to maintain. Templates with conditional sections (`{{#if has_rules}}`) make the prompt
composable and readable. Templates are compiled from embedded files at build time via
`rust_embed`, so there's no filesystem dependency at runtime.

---

## 9. Streaming Architecture: The Speed Advantage

Zed's streaming architecture is its biggest technical differentiator. Here's the full pipeline:

```
LLM generates tokens
    ↓
HTTP streaming response (SSE/JSON chunks)
    ↓
Provider client parses into LanguageModelCompletionEvent
    ↓
Thread event loop batches events
    ↓
Tool call JSON accumulates character by character
    ↓
Partial JSON fixer makes JSON parseable
    ↓
ToolInput channel sends partial input to tool
    ↓
StreamingEditFile tool receives partial old_text/new_text
    ↓
Tool Edit Parser extracts text chunks with delta tracking
    ↓
StreamingFuzzyMatcher finds location in buffer (incremental DP)
    ↓
StreamingDiff computes character-level operations
    ↓
Reindenter adjusts whitespace
    ↓
Buffer receives character operations
    ↓
GPUI renders updated text in editor
```

**End-to-end, this means:** The user sees characters appearing in their editor AS the LLM
generates them. There is no perceptible delay between the LLM deciding what to type and the
character appearing in the buffer.

**Why this matters:** Every other agent framework has a minimum latency of:
1. Wait for full tool call JSON (seconds for large edits)
2. Parse JSON and extract edit operations
3. Apply edits to file
4. Refresh editor to show changes

Zed collapses all of this to per-character latency (~milliseconds).

---

## 10. Subagent System: Controlled Parallelism

### Architecture

```
Parent Thread (depth 0)
    ├── spawn_agent("Implement feature A") → Child Thread (depth 1)
    ├── spawn_agent("Write tests for B")   → Child Thread (depth 1)
    └── spawn_agent("Fix linting issues")  → Child Thread (depth 1)
```

### Key Constraints

1. **Max depth 1.** Children cannot spawn grandchildren.
2. **No shared history.** Each child starts with only the spawn instruction.
3. **Parent settings propagate.** Model, thinking mode, and speed settings are inherited.
4. **Follow-up messages.** The parent can send additional messages to a child via session_id.
5. **Only final message returns.** The parent receives only the child's conclusion, not its
   full conversation.

### Why These Constraints Are Correct

**Max depth 1 prevents resource explosion.** If each agent could spawn 3 children, and each
child could spawn 3 more, 3 levels deep = 27 concurrent agents, each making LLM API calls.
This would exhaust rate limits instantly and cost a fortune.

**No shared history keeps children focused.** A parent thread with 50 messages of context about
feature A would confuse a child spawned to work on feature B. Clean context = focused execution.

**Only final message reduces noise.** A child might take 10 turns with 15 tool calls. The parent
doesn't need to see all of that -- just the conclusion ("I've implemented feature A and the tests
pass").

**Follow-up messages enable iterative delegation.** The parent can review the child's work and
say "also handle edge case X" without spawning a new child (which would lose the context of
the first iteration).

---

## 11. What Would Break Without Each Component

| Component | What Breaks | Severity |
|-----------|-------------|----------|
| Thread model | No conversation persistence, no multi-session | Critical |
| Edit Agent | Edit accuracy drops ~50%, frequent wrong-location edits | Critical |
| Streaming Fuzzy Matcher | Streaming edits impossible, must wait for full response | High |
| Tool Edit Parser | No streaming JSON → edit pipeline, edits delayed by seconds | High |
| Reindenter | Every edit has wrong indentation, needs manual fixing | Medium |
| Permission System | Agent has unrestricted access to filesystem and network | Critical |
| Symlink Escape Detection | Agent can read/write arbitrary system files | Critical |
| mtime Tracking | Agent overwrites user changes without warning | High |
| Event Batching | UI thrashes with per-token updates, performance degrades | Medium |
| Rate Limit Semaphore | Subagents deadlock with parent | High |
| ActionLog Integration | File deletions are untracked, no audit trail | Medium |
| Tool Kinds | Permission UI becomes unwieldy, no bulk approval | Low |
| Pattern Extraction | "Always allow" is all-or-nothing per tool, no granularity | Medium |
| Outline Mode | Large files consume entire context window | High |
| AST Context in Grep | Agent needs extra read_file calls per match | Medium |
| Batch Save/Restore | N tool calls instead of 1, wastes turns | Low |

---

## 12. Innovations Worth Stealing

### 12.1 The Edit Agent Pattern (HIGH VALUE)

**What:** Delegate file editing to a specialized secondary LLM call with current file content.
**Why it's valuable:** Dramatically improves edit accuracy by separating "what to do" from
"how to do it."
**How to adopt:** Create a specialized edit prompt that receives current file content + edit
description, returns structured search/replace operations.
**AVA equivalent:** Could enhance our `edit` tool to optionally use a secondary LLM call for
complex edits.

### 12.2 Streaming Edit Pipeline (HIGH VALUE)

**What:** Process LLM output character-by-character, applying edits as they stream.
**Why it's valuable:** Eliminates the delay between LLM generation and edit application.
**How to adopt:** Implement a streaming JSON parser + fuzzy matcher + streaming diff.
**AVA consideration:** Only relevant if/when AVA has a GUI. For CLI, this manifests as
streaming output display.

### 12.3 Asymmetric Fuzzy Match Costs (MEDIUM VALUE)

**What:** Use different costs for deletion (10), insertion (3), replacement (1) in edit matching.
**Why it's valuable:** Dramatically reduces false positive matches by penalizing the failure
modes that LLMs actually exhibit.
**How to adopt:** Apply to any fuzzy matching in edit tools.
**AVA equivalent:** Our `edit` tool could use this for fuzzy matching old_text blocks.

### 12.4 AST-Aware Grep Context (HIGH VALUE)

**What:** Include the parent syntax node for each grep match.
**Why it's valuable:** Gives the LLM the context it needs without extra tool calls.
**How to adopt:** Use tree-sitter to find the enclosing function/class for each match.
**AVA equivalent:** Our `grep` and `codesearch` tools could include syntax context.

### 12.5 Tool Output as Result<Output, Output> (MEDIUM VALUE)

**What:** Errors are the same type as success, not arbitrary strings.
**Why it's valuable:** Consistent, structured error messages to the LLM.
**How to adopt:** Define error schemas for each tool that match the output schema.
**AVA equivalent:** Currently tools return arbitrary error strings. Structured errors would
improve LLM error recovery.

### 12.6 mtime-Based Edit Safety (HIGH VALUE)

**What:** Track file modification time on read, check on edit.
**Why it's valuable:** Prevents the agent from overwriting concurrent changes.
**How to adopt:** Store mtime on every file read, compare before editing.
**AVA equivalent:** Our tools don't currently track this. Adding it would prevent a class of
silent data loss bugs.

### 12.7 Pattern-Based Permission Extraction (MEDIUM VALUE)

**What:** Extract regex patterns from tool inputs for "always allow" rules.
**Why it's valuable:** Granular permissions without per-invocation prompts.
**How to adopt:** For terminal commands, extract command + subcommand patterns. For file
operations, extract directory patterns.
**AVA equivalent:** Our permission system could add pattern-based "always allow" rules.

### 12.8 Outline Mode for Large Files (MEDIUM VALUE)

**What:** Return tree-sitter outline instead of full content for files > 16KB.
**Why it's valuable:** Prevents large files from consuming the entire context window.
**How to adopt:** Use tree-sitter to generate outlines (function signatures, class definitions).
**AVA equivalent:** Our `read_file` tool could auto-outline large files.

### 12.9 Compile-Time Tool Name Validation (LOW VALUE for TS)

**What:** Macro validates no duplicate tool names at compile time.
**Why it's valuable:** Catches a subtle bug class before runtime.
**How to adopt:** In TypeScript, use a build-time check or test that validates tool name
uniqueness.
**AVA equivalent:** We could add a test that asserts no duplicate tool names in the registry.

### 12.10 Sensitive Settings Protection (MEDIUM VALUE)

**What:** Prevent agent from modifying its own configuration files.
**Why it's valuable:** Prevents prompt injection from disabling safety features.
**How to adopt:** Blacklist configuration directories from write operations.
**AVA equivalent:** We should protect `.ava/`, `.config/`, and similar directories.

---

## 13. Weaknesses and Gaps

### What Zed Does NOT Have

| Capability | Status | Impact |
|------------|--------|--------|
| Hierarchical delegation | Only flat subagents (depth 1) | Cannot decompose large tasks |
| Task planning / todo | No explicit planning tools | Agent works purely reactively |
| Memory / RAG | No cross-session memory | Cannot learn from past sessions |
| Git integration tools | No built-in git tools (uses terminal) | Verbose for common operations |
| Session branching | No conversation forking | Cannot explore alternative approaches |
| Token tracking / compaction | Basic token counting only | No intelligent context management |
| Batch tool execution | No parallel tool dispatch | Tools run sequentially within a turn |
| Code analysis tools | No LSP-based go-to-definition, find-references | Agent uses grep instead |
| Custom tool definition | MCP only (no JS/TS plugin API) | Extension requires MCP server |
| Skill / recipe system | No reusable workflows | Cannot automate repetitive patterns |

### Architectural Limitations

1. **Single-file monster.** `thread.rs` is 4,144 lines. This makes the agent loop hard to
   understand, test, and modify independently. Most of the tool infrastructure (traits, input
   streaming, permissions) is in the same file as the turn loop.

2. **Tight GPUI coupling.** Every component uses GPUI entities (`Entity<T>`, `Context<T>`).
   This makes the agent logic untestable outside of a GPUI test harness and impossible to
   use in a headless/CLI context without the full GPUI runtime.

3. **Zed Cloud dependency.** Web search requires Zed Cloud. Some features (Zeta prediction)
   are Zed-proprietary. This creates vendor lock-in for key capabilities.

4. **No context compaction.** When the conversation grows too long, Zed doesn't automatically
   compress or summarize earlier messages. The only solution is thread summarization (which
   creates a new shorter summary, not compacting the existing thread).

5. **Sequential tool execution within a turn.** While subagents run in parallel, tools within
   a single turn run sequentially (wait for each to complete before starting the next). This
   means 5 independent file reads happen one after another.

6. **No structured planning.** There's no todo list, task breakdown, or planning mode. The
   agent is purely reactive -- it processes messages and makes tool calls. For complex tasks,
   this means the agent can lose track of what it's doing.

---

## 14. Comparison to AVA

### Where Zed Leads

| Area | Zed Advantage | AVA Gap |
|------|---------------|---------|
| Streaming edits | Character-by-character in editor | No GUI streaming (CLI-only) |
| Edit accuracy | EditAgent with fuzzy matching | Direct edit tool, no secondary LLM |
| LSP diagnostics | Built-in diagnostics tool | AVA has 9 LSP tools (actually stronger) |
| Visual diff review | Multibuffer diff pane | CLI diff display |
| Inline assistant | Separate in-place editing pipeline | No inline editing mode |
| Edit safety | mtime tracking on all reads | No mtime tracking |
| Symlink security | Full escape detection with worktree metadata | Basic path validation |

### Where AVA Leads

| Area | AVA Advantage | Zed Gap |
|------|---------------|---------|
| Tool count | 55+ tools | 17 built-in tools |
| Delegation | 3-tier Praxis hierarchy | Flat subagents only |
| Memory | Long-term memory + RAG | No cross-session memory |
| Extension API | 8 registration methods, middleware, hooks | MCP only |
| LSP tools | 9 LSP tools (go-to-def, references, etc.) | Only diagnostics tool |
| Session management | Forking, checkpoints, FTS5 search | Basic save/load |
| Context management | Token tracking + compaction | No compaction |
| Git integration | Built-in git tools | Terminal-only git |
| Planning | Todo lists, structured planning | No planning tools |
| Batch execution | Parallel tool dispatch | Sequential within turn |

### Strategic Takeaways

1. **Steal the Edit Agent pattern.** Zed's biggest accuracy advantage comes from the secondary
   LLM call for edits. AVA should consider this for complex edits.

2. **Steal mtime tracking.** Simple to implement, prevents a class of data loss bugs.

3. **Steal AST-aware grep context.** We already have tree-sitter. Adding syntax context to
   grep results would reduce follow-up tool calls.

4. **Steal pattern-based permissions.** More granular than per-tool approval, less annoying
   than per-invocation prompts.

5. **Don't chase streaming edits.** This is only valuable in a GUI editor. For CLI, it's
   irrelevant. Focus on what makes CLI agents powerful: planning, delegation, memory.

6. **Don't worry about the GUI gap.** Zed's advantages are inherently GUI-based. AVA's
   advantages are architectural. These don't compete directly -- they serve different
   workflows (IDE-integrated vs. terminal-native).

---

## Appendix: File Reference

### Core Agent Files Read

| File | Lines | Key Contents |
|------|-------|-------------|
| `crates/agent/src/thread.rs` | ~4,144 | Thread struct, AgentTool trait, turn loop, tool input streaming, tool permissions, retry logic, event batching, title/summary generation |
| `crates/agent/src/agent.rs` | ~large | NativeAgent, session management, model management, MCP integration |
| `crates/agent/src/tools.rs` | ~small | `tools!` macro, compile-time name validation, tool list |
| `crates/agent/src/templates.rs` | ~small | Handlebars template system, RustEmbed, SystemPromptTemplate |
| `crates/agent/src/outline.rs` | ~small | Tree-sitter outline generation for large files (16KB threshold) |
| `crates/agent/src/pattern_extraction.rs` | ~354 | Regex pattern extraction for "always allow" rules |

### Edit Agent Files

| File | Lines | Key Contents |
|------|-------|-------------|
| `crates/agent/src/edit_agent/edit_parser.rs` | ~1,094 | XML + DiffFenced edit format parsers, LLM quirk handling |
| `crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs` | ~802 | Incremental DP fuzzy matching, asymmetric costs, line hint disambiguation |
| `crates/agent/src/edit_agent/reindent.rs` | ~214 | Streaming re-indentation with indent delta computation |

### Tool Files

| File | Key Innovation |
|------|----------------|
| `tools/terminal_tool.rs` | 16KB output limit, working dir validation |
| `tools/edit_file_tool.rs` | EditAgent delegation, mtime tracking, unsaved change detection |
| `tools/streaming_edit_file_tool.rs` | Feature-flagged streaming edits, transparent tool name swap |
| `tools/read_file_tool.rs` | Outline mode for large files, image support, mtime recording |
| `tools/grep_tool.rs` | AST-aware context (parent syntax node per match) |
| `tools/find_path_tool.rs` | Worktree snapshot search (no filesystem I/O) |
| `tools/diagnostics_tool.rs` | Real LSP diagnostics, project-wide summary mode |
| `tools/spawn_agent_tool.rs` | Follow-up messages, settings propagation |
| `tools/tool_permissions.rs` | Symlink escape detection via worktree metadata, sensitive settings |
| `tools/tool_edit_parser.rs` | Streaming JSON to edit events, delta tracking |
| `tools/save_file_tool.rs` | Batch saving, format-on-save integration |
| `tools/delete_path_tool.rs` | ActionLog integration for deletion tracking |
| `tools/web_search_tool.rs` | Zed Cloud routing |
| `tools/fetch_tool.rs` | HTML-to-Markdown, Wikipedia special handler, URL pattern permissions |
