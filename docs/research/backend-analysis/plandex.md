# Plandex Backend Architecture Analysis

> Go-based AI coding agent with multi-file planning, client-server architecture, and git-backed versioning. ~7k GitHub stars, now shut down.

---

## 1. Project Structure

Plandex is a client-server application written entirely in Go:

```
plandex/
├── app/
│   ├── cli/                  # CLI client (Cobra-based)
│   │   ├── api/              # HTTP client methods
│   │   ├── cmd/              # 50+ CLI commands
│   │   ├── plan_exec/        # Client-side plan execution
│   │   ├── stream/           # SSE stream consumer
│   │   ├── stream_tui/       # Terminal UI for streaming
│   │   ├── auth/             # Authentication
│   │   ├── fs/               # File system helpers
│   │   ├── lib/              # Shared CLI libraries
│   │   └── main.go           # Entry point
│   ├── server/               # Go HTTP server
│   │   ├── handlers/         # HTTP handlers (25 files)
│   │   ├── model/            # LLM client + plan execution
│   │   │   ├── plan/         # Core agent loop (35 files)
│   │   │   ├── prompts/      # All LLM prompts (19 files)
│   │   │   └── parse/        # Response parsers
│   │   ├── db/               # PostgreSQL + Git ops (35 files)
│   │   ├── routes/           # Gorilla mux routing
│   │   ├── syntax/           # Tree-sitter parsing + validation
│   │   ├── diff/             # Diff generation
│   │   ├── hooks/            # Server-side hooks
│   │   └── main.go           # Entry point
│   ├── shared/               # Shared types (40 files)
│   │   ├── ai_models_*.go    # Model definitions, providers, packs, roles
│   │   ├── data_models.go    # Core domain types
│   │   ├── stream.go         # Stream message types
│   │   ├── context.go        # Context management
│   │   └── plan_status.go    # Plan status enum
│   └── docker-compose.yml    # PostgreSQL + LiteLLM
├── docs/
├── plans/
├── test/
└── scripts/
```

Key files by line count:
- `app/server/model/plan/tell_exec.go` — 685 lines (main agent loop)
- `app/server/model/plan/tell_stream_processor.go` — 752 lines (stream chunk processor)
- `app/server/model/client.go` — 561 lines (LLM client)
- `app/server/db/git.go` — 785 lines (git operations)
- `app/server/db/locks.go` — 669 lines (distributed locking)

---

## 2. CLI Commands

The CLI uses Cobra and exposes 50+ commands. Without arguments, it starts a REPL.

### Core Plan Commands

| Command | File | Purpose |
|---------|------|---------|
| `new` | `cmd/new.go` | Create a new plan |
| `tell` | `cmd/tell.go` | Send prompt to plan |
| `continue` | `cmd/continue.go` | Continue current plan |
| `build` | `cmd/build.go` | Build pending changes |
| `apply` | `cmd/apply.go` | Apply changes to files |
| `diffs` | `cmd/diffs.go` | View pending diffs |
| `reject` | `cmd/reject.go` | Reject pending changes |
| `rewind` | `cmd/rewind.go` | Rewind plan to earlier state |
| `stop` | `cmd/stop.go` | Stop active plan |

### Plan Management

| Command | File | Purpose |
|---------|------|---------|
| `plans` | `cmd/plans.go` | List all plans |
| `current` | `cmd/current.go` | Show current plan |
| `cd` | `cmd/cd.go` | Switch to plan by name |
| `rename` | `cmd/rename.go` | Rename plan |
| `archive` | `cmd/archive.go` | Archive a plan |
| `unarchive` | `cmd/unarchive.go` | Unarchive a plan |
| `delete-plan` | `cmd/delete_plan.go` | Delete a plan |
| `ps` | `cmd/ps.go` | Show running plans |

### Context Commands

| Command | File | Purpose |
|---------|------|---------|
| `load` | `cmd/load.go` | Load files/URLs/notes into context |
| `ls` | `cmd/ls.go` | List context items |
| `rm` | `cmd/rm.go` | Remove context items |
| `clear` | `cmd/clear.go` | Clear all context |
| `context show` | `cmd/context_show.go` | Show context body |
| `update` | `cmd/update.go` | Update outdated context |

### Branching

| Command | File | Purpose |
|---------|------|---------|
| `branches` | `cmd/branches.go` | List branches |
| `checkout` | `cmd/checkout.go` | Switch branch |
| `delete-branch` | `cmd/delete_branch.go` | Delete branch |

### Model Configuration

| Command | File | Purpose |
|---------|------|---------|
| `models` | `cmd/models.go` | List available models |
| `set-model` | `cmd/set_model.go` | Set model for a role |
| `model-packs` | `cmd/model_packs.go` | List/manage model packs |
| `set` | `cmd/set_config.go` | Update plan settings |

### Other

| Command | File | Purpose |
|---------|------|---------|
| `convo` | `cmd/convo.go` | Show conversation history |
| `summary` | `cmd/summary.go` | Show plan summary |
| `log` | `cmd/log.go` | Show plan logs |
| `connect` | `cmd/connect.go` | Connect to plan stream |
| `sign-in` | `cmd/sign_in.go` | Sign in to server |
| `invite` | `cmd/invite.go` | Invite user to org |
| `chat` | `cmd/chat.go` | Chat-only mode |
| `debug` | `cmd/debug.go` | Debug mode |
| `browser` | `cmd/browser.go` | Open web dashboard |

---

## 3. Agent Loop — The Tell/Build Pipeline

The core agent loop is Plandex's most complex subsystem. It operates in a **two-phase pipeline**: Planning (the "tell" phase) and Building (the "build" phase).

### 3.1 Phase Overview

```
User Prompt
    │
    ├─→ Phase 1: PLANNING
    │   ├─→ Context Phase (optional, auto-context)
    │   │   └─ Architect model selects files from project map
    │   └─→ Tasks Phase
    │       └─ Planner model creates subtask list
    │
    ├─→ Phase 2: IMPLEMENTATION
    │   └─ Coder model writes code for current subtask
    │       └─ Outputs <PlandexBlock> XML tags with file changes
    │
    └─→ Phase 3: BUILD (concurrent with implementation)
        └─ For each file operation detected in stream:
            ├─→ Auto-apply (tree-sitter based)
            ├─→ Validation (LLM-based verify + fix)
            └─→ Store result with git commit
```

### 3.2 Tell Execution Flow

Entry point: `app/server/model/plan/tell_exec.go`

```go
// Tell() is called from the handler
func Tell(params TellParams) error {
    // 1. Activate the plan (create active plan state)
    activatePlan(clients, plan, branch, auth, prompt, ...)

    // 2. Launch the execution in a goroutine
    go execTellPlan(execTellPlanParams{...})
}

func execTellPlan(params execTellPlanParams) {
    // 1. Load plan state from DB (subtasks, convo, summaries, context)
    state.loadTellPlan()

    // 2. Resolve current stage: Planning or Implementation
    state.resolveCurrentStage()

    // 3. Select model config based on stage
    //    - Planning/Context → Architect model
    //    - Planning/Tasks   → Planner model
    //    - Implementation   → Coder model

    // 4. Dry-run token calculation (without context)
    state.dryRunCalculateTokensWithoutContext()

    // 5. Format context into system prompt parts
    state.formatModelContext(...)

    // 6. Build system prompt
    state.getTellSysPrompt(...)

    // 7. Add conversation messages (with summarization if needed)
    state.addConversationMessages()

    // 8. Send LLM request and listen to stream
    state.doTellRequest()
    go state.listenStream(stream)
}
```

### 3.3 Stage Resolution

The agent resolves what phase to operate in based on conversation state:

```go
// app/server/model/plan/tell_stage.go
func (state *activeTellStreamState) resolveCurrentStage() {
    // If last message was from user → Planning stage
    // If last assistant message had DidMakePlan flag → Implementation stage
    // If already in Implementation → Stay in Implementation

    // Within Planning:
    //   - If auto-context enabled + has context map → Context phase first
    //   - Otherwise → Tasks phase directly
}
```

The two stages have distinct prompts and model roles:

| Stage | Phase | Model Role | Purpose |
|-------|-------|------------|---------|
| Planning | Context | Architect | Select relevant files from project map |
| Planning | Tasks | Planner | Create subtask breakdown |
| Implementation | - | Coder | Write code for current subtask |

### 3.4 Stream Processing

The stream processor (`tell_stream_processor.go`) handles LLM output in real-time:

- **Parses `<PlandexBlock>` XML tags** — The LLM outputs file changes wrapped in custom XML:
  ```xml
  <PlandexBlock lang="typescript" path="src/index.ts">
  // file content here
  </PlandexBlock>
  ```
- **Replaces XML tags with markdown** for display to the user (swaps `<PlandexBlock>` for triple backticks)
- **Detects file operations** (create, update, move, remove, reset)
- **Handles missing files** — If the LLM writes to a file not in context, pauses and asks the user what to do
- **Uses a stop sequence** `<PlandexFinish/>` to signal completion

### 3.5 Build Pipeline

When operations are detected in the stream, builds are queued concurrently:

```go
// app/server/model/plan/build_exec.go
func (fileState *activeBuildStreamFileState) buildFile() {
    // 1. Resolve pre-build state (from context or current plan)
    // 2. Handle special operations: move, remove, reset
    // 3. For new files: store directly
    // 4. For existing files: use structured edits pipeline
    fileState.buildStructuredEdits()
}
```

The structured edits pipeline (`build_structured_edits.go`):

```
Proposed Change
    │
    ├─→ ApplyChanges (tree-sitter based, local)
    │   └─ Returns NeedsVerifyReasons + syntax errors
    │
    ├─→ Fast Apply (hook-based, optional)
    │
    └─→ If auto-apply fails:
        └─ Build Race: run LLM validation + fast apply in parallel
            └─ Pick the first valid result
```

### 3.6 Validation and Fix Loop

The validation system (`build_validate_and_fix.go`) uses an LLM to verify and fix edits:

```go
const MaxValidationFixAttempts = 3

func buildValidateLoop(...) {
    for numAttempts < maxAttempts {
        // 1. Get diff between original and updated
        // 2. Send to Builder model with validation prompt
        // 3. Parse XML response for <PlandexCorrect/> or <PlandexReplacements>
        // 4. Apply line-numbered replacements
        // 5. Check syntax with tree-sitter
        // 6. If valid → return; else → retry with stronger model
    }
}
```

The validation uses line-numbered XML replacements:
```xml
<PlandexReplacements>
  <Replacement>
    <Old>pdx-15 | old code here</Old>
    <New>new code here</New>
  </Replacement>
</PlandexReplacements>
```

### 3.7 Auto-Continue Logic

After each tell/implementation round, the system decides whether to continue:

```go
// app/server/model/plan/tell_stream_status.go
func willContinuePlan(params) bool {
    // Planning stage:
    //   - After Context phase → always continue to Tasks phase
    //   - After Tasks phase → continue if new subtasks exist
    // Implementation stage:
    //   - Continue if subtasks remain (up to MaxAutoContinueIterations=200)
    //   - Stop if all subtasks finished
}
```

---

## 4. Client-Server Architecture

### 4.1 Communication Pattern

Plandex uses a **REST API + SSE streaming** architecture:

```
CLI (Cobra)                          Server (Gorilla mux)
    │                                      │
    ├──POST /plans/{id}/{branch}/tell──────→│ Start plan execution
    │                                      │   (returns immediately)
    │                                      │
    ├──PATCH /plans/{id}/{branch}/connect──→│ Subscribe to SSE stream
    │←─────────StreamMessage chunks────────│ Real-time updates
    │                                      │
    ├──GET /plans/{id}/{branch}/context────→│ List context
    ├──POST /plans/{id}/{branch}/context───→│ Load context
    ├──PATCH /plans/{id}/{branch}/apply────→│ Apply changes
    └──DELETE /plans/{id}/{branch}/stop────→│ Stop plan
```

### 4.2 API Routes

The server registers ~60 routes in `app/server/routes/routes.go`:

- **Authentication**: `/accounts/*` — sign in, sign out, email verification
- **Organizations**: `/orgs/*` — list, create, manage
- **Projects**: `/projects/*` — CRUD
- **Plans**: `/plans/*` — CRUD, status, branches, diffs, rewind
- **Context**: `/plans/{id}/{branch}/context` — load, update, delete
- **Execution**: `/plans/{id}/{branch}/tell` (POST, streaming), `/plans/{id}/{branch}/build` (PATCH, streaming)
- **Models**: `/custom_models/*`, `/model_sets/*`, `/default_settings/*`
- **File Maps**: `/file_map` (POST), `/plans/{id}/{branch}/load_cached_file_map` (POST)

### 4.3 Streaming Protocol

Messages use a custom separator `@@PX@@` and are JSON-encoded:

```go
// app/shared/stream.go
const STREAM_MESSAGE_SEPARATOR = "@@PX@@"

type StreamMessageType string
const (
    StreamMessageStart             = "start"
    StreamMessageReply             = "reply"           // LLM response chunk
    StreamMessageBuildInfo         = "buildInfo"       // Build progress
    StreamMessageDescribing        = "describing"      // Generating description
    StreamMessageRepliesFinished   = "repliesFinished" // All replies done
    StreamMessagePromptMissingFile = "promptMissingFile" // Need user input
    StreamMessageLoadContext       = "loadContext"     // Auto-load files
    StreamMessageFinished          = "finished"        // Plan complete
    StreamMessageError             = "error"
)
```

### 4.4 Active Plan State

Plans have an in-memory active state during execution:

```go
// app/server/model/plan/state.go
var activePlans types.SafeMap[*types.ActivePlan]

// ActivePlan holds all runtime state for an executing plan:
// - Context (cancellation)
// - StreamDoneCh (completion/error channel)
// - CurrentReplyContent (accumulated LLM output)
// - BuildQueuesByPath (concurrent build queues per file)
// - ContextsByPath (loaded file contexts)
// - MissingFileResponseCh (user input channel)
// - AutoLoadContextCh (auto-load coordination)
```

---

## 5. LLM Providers

### 5.1 Provider Architecture

Plandex uses a dual-path approach:
- **OpenAI and OpenRouter** — Direct HTTP calls to OpenAI-compatible APIs
- **All other providers** — Proxied through **LiteLLM** (Python, runs as sidecar)

```go
// app/server/main.go
model.EnsureLiteLLM(2) // Start LiteLLM proxy on port 4000
```

### 5.2 Supported Providers

From `app/shared/ai_models_providers.go`:

| Provider | Constant | Base URL | Auth |
|----------|----------|----------|------|
| OpenAI | `ModelProviderOpenAI` | `https://api.openai.com/v1` | `OPENAI_API_KEY` |
| OpenRouter | `ModelProviderOpenRouter` | `https://openrouter.ai/api/v1` | `OPENROUTER_API_KEY` |
| Anthropic | `ModelProviderAnthropic` | LiteLLM proxy | `ANTHROPIC_API_KEY` |
| Anthropic (Claude Max) | `ModelProviderAnthropicClaudeMax` | LiteLLM proxy | OAuth token |
| Google AI Studio | `ModelProviderGoogleAIStudio` | LiteLLM proxy | `GEMINI_API_KEY` |
| Google Vertex AI | `ModelProviderGoogleVertex` | LiteLLM proxy | Service account |
| Azure OpenAI | `ModelProviderAzureOpenAI` | LiteLLM proxy | `AZURE_OPENAI_API_KEY` |
| Amazon Bedrock | `ModelProviderAmazonBedrock` | LiteLLM proxy | AWS credentials |
| DeepSeek | `ModelProviderDeepSeek` | LiteLLM proxy | `DEEPSEEK_API_KEY` |
| Perplexity | `ModelProviderPerplexity` | LiteLLM proxy | `PERPLEXITY_API_KEY` |
| Ollama | `ModelProviderOllama` | LiteLLM proxy | None (local) |
| Custom | `ModelProviderCustom` | User-defined | User-defined |

### 5.3 Client Implementation

All providers go through a unified streaming interface:

```go
// app/server/model/client.go
func CreateChatCompletionStream(
    clients map[string]ClientInfo,
    authVars map[string]string,
    modelConfig *shared.ModelRoleConfig,
    ...
) (*ExtendedChatCompletionStream, error) {
    // 1. Resolve provider from model config
    // 2. Handle fallbacks on error
    // 3. Apply provider-specific config (Azure deployments, Bedrock regions, etc.)
    // 4. Stream via SSE with retry logic
}
```

Retry constants:
```go
ACTIVE_STREAM_CHUNK_TIMEOUT          = 60s
USAGE_CHUNK_TIMEOUT                  = 10s
MAX_ADDITIONAL_RETRIES_WITH_FALLBACK = 1
MAX_RETRIES_WITHOUT_FALLBACK         = 3
```

### 5.4 Model Packs (Role-Based Routing)

Plandex assigns different models to different roles, grouped into "Model Packs":

```go
// app/shared/ai_models_roles.go
type ModelRole string
const (
    ModelRolePlanner          = "planner"          // Plans tasks
    ModelRoleCoder            = "coder"            // Writes code
    ModelRoleArchitect        = "architect"        // Selects context
    ModelRolePlanSummary      = "summarizer"       // Summarizes conversations
    ModelRoleBuilder          = "builder"          // Validates/fixes edits
    ModelRoleWholeFileBuilder = "whole-file-builder"
    ModelRoleName             = "names"            // Names plans
    ModelRoleCommitMsg        = "commit-messages"  // Writes commit messages
    ModelRoleExecStatus       = "auto-continue"    // Decides if plan should continue
)
```

Built-in packs from `app/shared/ai_models_packs.go`:

| Pack | Planner | Coder | Builder | Description |
|------|---------|-------|---------|-------------|
| `daily-driver` | Claude Sonnet 4 | Claude Sonnet 4 | o4-mini-medium | Default, up to 2M context |
| `reasoning` | Sonnet 4 Thinking | Sonnet 4 Thinking | o4-mini-medium | Reasoning-enabled |
| `strong` | o3-high | Sonnet 4 Thinking | o4-mini-high | Difficult tasks |
| `cheap` | o4-mini-medium | GPT-4.1 | o4-mini-low | Cost-effective |
| `oss` | DeepSeek R1 | DeepSeek V3 | DeepSeek R1 | Open source |
| `anthropic` | Claude Sonnet 4 | Claude Sonnet 4 | Claude Sonnet 4 | Anthropic-only |
| `openai` | GPT-4.1 | GPT-4.1 | o4-mini-medium | OpenAI-only |
| `google` | Gemini 2.5 Pro | Gemini 2.5 Flash | Gemini 2.5 Pro | Google-only |
| `ollama` | Qwen3-32B | Qwen3-32B | Devstral Small | Local Ollama |

Each role supports **large-context fallback** (e.g., if input exceeds model limits, falls back to Gemini 2.5 Pro for its 1M+ context) and **error fallback** (switch provider on failure).

---

## 6. Context & Token Management

### 6.1 Context Types

From `app/shared/data_models.go`:

| Type | Constant | Description |
|------|----------|-------------|
| File | `ContextFileType` | Source code files |
| URL | `ContextURLType` | Web page content |
| Note | `ContextNoteType` | User-written notes |
| Directory Tree | `ContextDirectoryTreeType` | Directory listings |
| Piped Data | `ContextPipedDataType` | Stdin pipe |
| Image | `ContextImageType` | Image files |
| Map | `ContextMapType` | Tree-sitter project maps |

### 6.2 Context Limits

```go
// app/shared/context.go
MaxContextBodySize           = 25 MB      // Single item
MaxContextCount              = 1000       // Items per plan
MaxContextMapPaths           = 3000       // Files in project map
MaxContextMapSingleInputSize = 500 KB     // Single file for mapping
MaxContextMapTotalInputSize  = 250 MB     // Total map input
MaxTotalContextSize          = 1 GB       // Total context storage
```

### 6.3 Smart Context (Per-Subtask Filtering)

During implementation, Plandex filters context to only files referenced by the current subtask:

```go
// app/server/model/plan/tell_context.go
if currentStage.TellStage == shared.TellStageImplementation &&
   smartContextEnabled && state.currentSubtask != nil {
    // Only include files listed in currentSubtask.UsesFiles
    for _, path := range state.currentSubtask.UsesFiles {
        uses[path] = true
    }
}
```

This keeps implementation prompts focused and within token limits.

### 6.4 Auto-Context (Architect Phase)

When auto-context is enabled, the planning stage has two phases:

1. **Context Phase**: The Architect model reviews the project map and selects relevant files
2. **Tasks Phase**: With context loaded, the Planner creates subtasks

The context phase extracts file paths from backtick-quoted strings in the LLM response:

```go
// app/server/model/plan/tell_context.go
func (state *activeTellStreamState) checkAutoLoadContext() {
    // Parse backtick-quoted paths from reply
    matches := pathRegex.FindAllStringSubmatch(activePlan.CurrentReplyContent, -1)

    // For each path in project:
    //   - If already in context → mark as "activate"
    //   - If not in context → mark for auto-load
}
```

### 6.5 Project Maps (Tree-Sitter)

Project maps are generated server-side using tree-sitter parsing:

```
app/server/syntax/file_map/     # Tree-sitter map generation
app/server/handlers/file_maps.go # HTTP handler with queue
```

Maps are:
- Generated in batches (max 500 files, 10MB per batch)
- Cached per-project with SHA-based invalidation
- Show file structure with token counts for each file

### 6.6 Conversation Summarization

When conversations exceed token limits, Plandex generates summaries:

```go
// app/server/model/plan/tell_summary.go
func (state *activeTellStreamState) addConversationMessages() bool {
    // If total tokens exceed max:
    //   1. Find earliest summary that brings tokens under limit
    //   2. Replace old messages with summary
    //   3. Keep recent messages after summary

    // Summary is generated by PlanSummary model role
    // Stored in DB for reuse across iterations
}
```

The summarization model is typically a cheaper model (o4-mini-low or GPT-4.1-mini).

---

## 7. Multi-File Planning

This is Plandex's signature feature. The planning system works through structured subtasks.

### 7.1 Subtask Structure

```go
// app/shared/data_models.go
type Subtask struct {
    Title       string   `json:"title"`
    Description string   `json:"description"`
    UsesFiles   []string `json:"usesFiles"`
    IsFinished  bool     `json:"isFinished"`
}
```

### 7.2 Planning Flow

1. **User sends prompt** via `tell` command
2. **Planning phase** (Planner model):
   - Receives project map + loaded context
   - Outputs a `### Tasks` section with numbered subtasks
   - Each subtask lists files it will modify (`UsesFiles`)
3. **Implementation phase** (Coder model):
   - Receives only the files for the current subtask (smart context)
   - Writes code wrapped in `<PlandexBlock>` tags
   - After each subtask, the exec-status model checks if it's done
4. **Auto-continue**:
   - If subtask is finished, mark it and move to next
   - Continue until all subtasks are done or max iterations reached

### 7.3 Subtask Parsing

```go
// app/server/model/plan/tell_subtasks.go
func (state *activeTellStreamState) checkNewSubtasks() {
    content := activePlan.CurrentReplyContent
    subtasks := parse.ParseSubtasks(content) // Parses ### Tasks section

    // Merge with existing subtasks:
    //   - Keep finished subtasks
    //   - Add new subtasks if they don't exist
    //   - Track current subtask pointer
}

func (state *activeTellStreamState) checkRemoveSubtasks() {
    // Parses ### Remove Tasks section
    // Removes subtasks by title
}
```

### 7.4 File Operations

The LLM can output multiple operation types:

```go
type OperationType string
const (
    OperationTypeFile   = "file"   // Create/update file
    OperationTypeMove   = "move"   // Move file (src → dest)
    OperationTypeRemove = "remove" // Delete file
    OperationTypeReset  = "reset"  // Reset to original state
)
```

Operations are detected during stream processing and immediately queued for building:

```go
// app/server/model/plan/tell_stream_processor.go
func (state *activeTellStreamState) handleNewOperations(parserRes) {
    for _, op := range operations {
        if req.BuildMode == shared.BuildModeAuto {
            buildState.queueBuilds([]*types.ActiveBuild{{
                ReplyId:     replyId,
                Path:        op.Path,
                FileContent: op.Content,
                // ...
            }})
        }
    }
}
```

### 7.5 Apply Script

Plandex supports an `_apply.sh` script for running commands during plan execution. The planner can create a `### Commands` section that generates shell commands to run after applying changes.

---

## 8. Session/Plan Management

### 8.1 Data Model Hierarchy

```
Org
 └── Project
      └── Plan
           └── Branch (default: "main")
                ├── Context (files, URLs, notes, maps)
                ├── ConvoMessages (user + assistant)
                ├── ConvoSummaries (compressed history)
                ├── Subtasks (plan breakdown)
                ├── PlanBuilds (per-file build records)
                ├── PlanFileResults (build outputs with replacements)
                ├── ConvoMessageDescriptions (commit messages)
                └── PlanApplies (applied change records)
```

### 8.2 Plan States

```go
// app/shared/plan_status.go
type PlanStatus string
const (
    PlanStatusDraft       = "draft"       // New, no activity
    PlanStatusReplying    = "replying"    // LLM is generating
    PlanStatusDescribing  = "describing"  // Generating commit message
    PlanStatusBuilding    = "building"    // Building file changes
    PlanStatusMissingFile = "missingFile" // Waiting for user input
    PlanStatusFinished    = "finished"    // Complete
    PlanStatusStopped     = "stopped"     // User stopped
    PlanStatusError       = "error"       // Error occurred
)
```

### 8.3 Versioning with Git

Each plan has a **server-side git repository**. Every change is committed:

```go
// app/server/db/git.go
func InitGitRepo(orgId, planId string) error {
    dir := getPlanDir(orgId, planId)
    exec.Command("git", "-C", dir, "init", "-b", "main")
    setGitConfig(dir, "user.email", "server@plandex.ai")
    setGitConfig(dir, "user.name", "Plandex")
}
```

The git repo stores plan file state (not source code). This enables:
- **Rewind**: Reset to any previous commit
- **Branching**: Create alternate plan paths
- **Apply**: Copy built files to user's project

### 8.4 Database (PostgreSQL)

The server uses PostgreSQL with the following key tables:

- `plans` — Plan metadata
- `branches` — Branch metadata with status and tokens
- `contexts` — Loaded context items per branch
- `convo_messages` — Conversation history
- `convo_summaries` — Compressed conversation history
- `subtasks` — Plan subtask breakdown
- `plan_builds` — Build records per file
- `plan_file_results` — Build outputs with replacements
- `convo_message_descriptions` — Generated commit messages
- `plan_applies` — Records of applied changes
- `repo_locks` — Distributed locking for git operations
- `lockable_plan_ids` — Lock targets

### 8.5 Distributed Locking

The locking system (`app/server/db/locks.go`) prevents concurrent git operations:

```go
// Read locks: allow concurrent reads on same branch, block writes
// Write locks: exclusive, block all other locks

type LockScope string
const (
    LockScopeRead  LockScope = "r"
    LockScopeWrite LockScope = "w"
)

// Features:
// - PostgreSQL-backed with SELECT FOR UPDATE
// - Heartbeat-based expiry (3s interval, 60s timeout)
// - Exponential backoff retry (6 retries, 300ms initial)
// - Automatic git index.lock cleanup
// - Branch checkout on lock acquisition
```

---

## 9. Git Integration

### 9.1 Server-Side Git

Every plan has its own git repository at `{data_dir}/{orgId}/{planId}/`. Operations include:

| Operation | Method | Purpose |
|-----------|--------|---------|
| `GitAddAndCommit` | `git add . && git commit` | Save plan state |
| `GitRewindToSha` | `git reset --hard {sha}` | Rewind plan |
| `GitCreateBranch` | `git checkout -b {name}` | Create plan branch |
| `GitDeleteBranch` | `git branch -D {name}` | Delete plan branch |
| `GitCheckoutBranch` | `git checkout {name}` | Switch branch |
| `GitClearUncommittedChanges` | `git reset --hard && git clean -df` | Cleanup on error |
| `GetGitCommitHistory` | `git log` | Plan history |

### 9.2 Client-Side Apply

When the user runs `plandex apply`, the CLI:
1. Fetches the current plan state from the server
2. Copies built files to the user's project directory
3. Records the apply in the database

### 9.3 Commit Messages

Commit messages are auto-generated using a dedicated model role (`ModelRoleCommitMsg`), typically a fast/cheap model like GPT-4.1-mini.

---

## 10. Unique Features

### 10.1 Planning-First Architecture

Unlike tool-calling agents (Claude Code, Cursor), Plandex separates planning from implementation at the architecture level. The planner creates a structured task list before any code is written. This enables:

- **Predictable execution**: Users see the plan before code changes
- **Smart context routing**: Each subtask only sees relevant files
- **Progress tracking**: Clear visibility into which tasks are done
- **Plan modification**: Users can add/remove tasks mid-execution

### 10.2 Concurrent Build Pipeline

File builds happen **in parallel with LLM streaming**. As the coder model outputs file blocks, they are immediately queued for building. Each file has its own build queue, so multiple files can be built concurrently.

### 10.3 Three-Tier Validation

File edits go through up to three validation tiers:
1. **Auto-apply** (tree-sitter): Fast, deterministic text patching
2. **Fast apply** (hook-based): External fast-apply service
3. **LLM validation** (Builder model): Full verification with up to 3 fix attempts, escalating to a stronger model

### 10.4 LiteLLM Sidecar

Instead of implementing each provider's API, Plandex runs LiteLLM as a sidecar proxy. Only OpenAI and OpenRouter get direct API calls; everything else goes through LiteLLM's unified interface.

### 10.5 Model Role Specialization

Nine distinct model roles allow mixing models for optimal cost/quality:
- Expensive reasoning models for planning
- Fast models for naming and commit messages
- Mid-tier models for code generation
- Small models for auto-continue decisions

### 10.6 Branch-Based Plan Management

Plans can be branched server-side using git branches, allowing users to explore alternate approaches without losing previous work. The locking system ensures branch operations are safe.

### 10.7 Missing File Detection

During implementation, if the LLM references a file that exists in the project but isn't loaded into context, the stream is paused and the user is prompted to decide what to do. This prevents hallucinated file overwrites.

### 10.8 XML-Based Output Format

Rather than using tool calls or JSON, Plandex uses a custom XML format (`<PlandexBlock>`, `<PlandexFileOps>`, `<PlandexFinish/>`) that the stream processor parses incrementally. This allows real-time parsing of partial output while maintaining structured data extraction.

---

## Appendix: Key Type Definitions

### Active Tell Stream State

The central state object during plan execution:

```go
// app/server/model/plan/tell_state.go (inferred from usage)
type activeTellStreamState struct {
    modelStreamId       string
    clients             map[string]model.ClientInfo
    authVars            map[string]string
    req                 *shared.TellPlanRequest
    auth                *types.ServerAuth
    currentOrgId        string
    currentUserId       string
    plan                *db.Plan
    branch              string
    iteration           int
    missingFileResponse shared.RespondMissingFileChoice

    // Loaded state
    settings            *shared.PlanSettings
    currentStage        shared.CurrentStage
    subtasks            []*db.Subtask
    currentSubtask      *db.Subtask
    convo               []*db.ConvoMessage
    summaries           []*db.ConvoSummary
    modelContext        []*db.Context
    currentPlanState    *shared.CurrentPlanState

    // Execution state
    messages            []types.ExtendedChatMessage
    tokensBeforeConvo   int
    totalRequestTokens  int
    replyId             string
    replyParser         *types.ReplyParser
    chunkProcessor      *chunkProcessor
    modelConfig         *shared.ModelRoleConfig
    baseModelConfig     *shared.BaseModelConfig
    activePlan          *types.ActivePlan
}
```

### Conversation Message Flags

```go
type ConvoMessageFlags struct {
    DidMakePlan           bool  // Created a task plan
    DidRemoveTasks        bool  // Removed tasks
    DidLoadContext        bool  // Auto-loaded context
    CurrentStage          CurrentStage
    IsChat                bool  // Chat-only (no code)
    DidWriteCode          bool  // Wrote file blocks
    DidCompleteTask       bool  // Finished a subtask
    DidCompletePlan       bool  // All subtasks done
    HasUnfinishedSubtasks bool
    HasError              bool
}
```
