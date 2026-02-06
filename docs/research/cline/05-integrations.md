# Cline Integrations

> Analysis of checkpoints, terminal execution, OAuth, and file processing integrations

---

## Overview

The Cline integrations directory contains 44+ specialized integration modules organized into 8 main categories handling cross-cutting concerns like checkpoint management, terminal command execution, editor interactions, and OAuth authentication.

---

## Integration Catalog

### 1. Checkpoints Integration

**Purpose:** Git-based version control for AI-generated changes without interfering with user's repository.

**Architecture:**
- **Shadow Git Repository**: Creates isolated Git repos per workspace
- **Storage**: `.cline/checkpoints/{cwdHash}/` in global storage
- **Lock Mechanism**: File-based distributed locking with retry logic

**Key Files:**
- `CheckpointTracker.ts` (512 lines) - Main orchestrator
- `CheckpointGitOperations.ts` - Git operations
- `CheckpointLockUtils.ts` - Multi-process locking
- `TaskCheckpointManager.ts` (914 lines) - Task-specific management

**Data Flow:**
```
TaskCheckpointManager.saveCheckpoint()
  → Initializes CheckpointTracker
  → CheckpointTracker.commit()
    → Acquires folder lock
    → Stages files via git add
    → Creates commit: "checkpoint-{cwdHash}-{taskId}"
    → Releases lock
```

**Key Operations:**
1. **Initialize**: Creates shadow Git repo, configures Git settings
2. **Save**: Creates checkpoint commit
3. **Restore**: Hard resets to previous checkpoint
4. **Diff**: Generates file-level changes between checkpoints
5. **Multi-Root**: Supports workspace hierarchies with hash-based isolation

### 2. Claude Code Integration

**Purpose:** Integrates Claude Code CLI as a subagent executor.

**Communication:**
- Execution via `execa` npm package
- JSON streaming on stdout
- Stream-based JSON messages (init, assistant, result, error)

**Configuration:**
- Max System Prompt: 65536 bytes
- Max Output Tokens: 32000
- Timeout: 10 minutes
- Buffer Size: 20 MB

**Disallowed Tools:** Task, Bash, Glob, Grep, LS, Read, Edit, MultiEdit, Write, etc.

### 3. CLI Subagents Integration

**Purpose:** Detects and transforms simplified Cline CLI commands.

**Pattern Matching:**
```regex
/^cline\s+(['"])(.+?)\1(\s+.*)?$/

Input:  cline "fix the bug"
Output: cline "fix the bug" --json -y
```

**Routing:**
- Subagent commands → StandaloneTerminalManager (hidden terminals)
- Regular commands → Configured terminal manager

### 4. Terminal Integration

**Purpose:** Unified command execution across VSCode terminals and standalone processes.

**Architecture:**
```
CommandExecutor
  ├─ For VSCode: VscodeTerminalManager
  ├─ For Subagents: StandaloneTerminalManager
  └─ orchestrateCommandExecution() [shared logic]
```

**Features:**

| Feature | Description |
|---------|-------------|
| Buffer Management | Flush at 512KB or 100 lines |
| Large Output | Switch to file-based logging at 10K lines / 50MB |
| Proceed While Running | Background tracking with log file |
| Timeout | Trigger "Proceed While Running" on timeout |
| Shell Integration | Warnings after 3+ issues in 1 hour |

**Configuration Constants:**
- `CHUNK_BYTE_SIZE` = 512KB
- `CHUNK_LINE_COUNT` = 100
- `MAX_BYTES_BEFORE_FILE` = 50MB
- `MAX_LINES_BEFORE_FILE` = 10000
- `BUFFER_STUCK_TIMEOUT_MS` = 30 seconds

### 5. Diagnostics Integration

**Purpose:** Detect and report code problems from language servers.

**Algorithm:**
```
Pre-edit diagnostics captured
  → File editing occurs
  → Post-edit diagnostics captured
  → getNewDiagnostics() filters only NEW problems
  → diagnosticsToProblemsString() formats for display
```

**Problem String Format:**
```
src/file.ts
- [ESLint Error] Line 42: Unexpected token
- [TypeScript Warning] Line 89: Type mismatch
```

### 6. Editor Integration

**Purpose:** Manages file editing via diff views and tracks user edits.

**DiffViewProvider Lifecycle:**

1. **open(relPath)**: Resolve path, save dirty docs, capture pre-edit diagnostics
2. **update(content, isFinal)**: Throttle updates, compute diff, scroll to changes
3. **saveChanges()**: Get pre/post save content, capture diagnostics, format patches
4. **revertChanges()**: Delete new files, restore modified files

**Features:**
- Streaming updates with 10/sec throttling
- Edit tracking (user, cline, auto-format)
- BOM and encoding handling
- Notebook output sanitization

### 7. Notifications Integration

**Purpose:** Cross-platform system notifications.

| Platform | Implementation |
|----------|----------------|
| macOS | osascript (AppleScript) |
| Windows | PowerShell (Windows.UI.Notifications) |
| Linux | notify-send (libnotify) |

### 8. OpenAI Codex OAuth Integration

**Purpose:** OAuth 2.0 authentication with OpenAI for Codex API access.

**OAuth Configuration:**
```typescript
{
  authorizationEndpoint: "https://auth.openai.com/oauth/authorize",
  tokenEndpoint: "https://auth.openai.com/oauth/token",
  clientId: "app_EMoamEEZ73f0CkXaXp7hrann",
  redirectUri: "http://localhost:1455/auth/callback",
  scopes: "openid profile email offline_access",
}
```

**PKCE Flow:**
1. `generateCodeVerifier()` → 43-128 char random string
2. `generateCodeChallenge(verifier)` → SHA256.base64url
3. `generateState()` → 32 hex chars (CSRF protection)
4. `buildAuthorizationUrl()` → Browser URL
5. User authorizes → Callback to localhost:1455
6. `exchangeCodeForTokens()` → Access + Refresh tokens
7. `extractAccountId()` → Parse JWT for ChatGPT account ID

**Token Management:**
- Auto-refresh with 5-min expiration buffer
- Concurrent refresh deduplication
- Invalid grant detection

### 9. File Processing Integrations

**Supported Formats:**
- `.pdf` → `extractTextFromPDF()` (via pdf-parse)
- `.docx` → `extractTextFromDOCX()` (via mammoth)
- `.ipynb` → `extractTextFromIPYNB()` (Jupyter notebooks)
- `.xlsx` → `extractTextFromExcel()` (ExcelJS)

**Limits:**
- Text files: 20MB
- Excel: 50,000 rows per sheet

**Link Preview:**
- Open Graph metadata extraction
- Image URL detection via HEAD request
- Proxy-aware networking

**Notebook Utilities:**
- Sanitizes outputs (removes image data)
- Context-aware handling (read vs write)

---

## Notable Features for Estela

### 1. Checkpoint System
Version control for changes without repo interference.

### 2. Distributed Locking
Multi-process safety for shared operations.

### 3. Terminal Output Management
Buffering, "Proceed While Running", background tracking.

### 4. OAuth 2.0 + PKCE
Secure third-party authentication with token refresh.

### 5. Diagnostics Tracking
Pre/post-edit problem detection.

### 6. Large Output File Logging
Prevents memory exhaustion.

### 7. Timeout Detection
User warnings and fallback behavior.

### 8. Edit Attribution
Track user edits vs AI edits vs auto-formatting.

### 9. Streaming Updates
Throttled, progressive content display.

### 10. Multi-Format File Extraction
PDF, DOCX, Excel, Jupyter support.
