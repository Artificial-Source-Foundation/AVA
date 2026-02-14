# Gemini CLI VS Code Extension Analysis

> Analysis of the Gemini CLI Companion VS Code Extension architecture and patterns.

**Source Location:** `/home/xn3/Projects/Personal/AVA/docs/reference-code/gemini-cli/packages/vscode-ide-companion/`

---

## Overview

The Gemini CLI Companion is a VS Code extension that bridges the Gemini CLI (running in a terminal) with VS Code's editor capabilities. It enables:

1. **Editor Context Awareness** - CLI gains awareness of open files, cursor position, and selected text
2. **Native Diff Views** - Code changes suggested by the CLI are displayed in VS Code's native diff editor
3. **Bidirectional Communication** - Real-time sync between CLI and IDE via MCP (Model Context Protocol)

### Architecture Summary

```
┌─────────────────────────────────────────────────────────────────────┐
│                         VS Code Extension                           │
├─────────────────────────────────────────────────────────────────────┤
│  extension.ts                                                       │
│  ├── Activation & lifecycle management                              │
│  ├── Command registration                                           │
│  └── DiffManager + IDEServer initialization                         │
├─────────────────────────────────────────────────────────────────────┤
│  ide-server.ts (MCP Server)                                         │
│  ├── Express HTTP server on localhost:random-port                   │
│  ├── StreamableHTTPServerTransport for MCP                          │
│  ├── Auth token (Bearer) + CORS + Host header validation            │
│  └── Registers openDiff/closeDiff tools                             │
├─────────────────────────────────────────────────────────────────────┤
│  diff-manager.ts                                                    │
│  ├── DiffContentProvider (virtual documents)                        │
│  ├── Shows diff via vscode.diff command                             │
│  └── Emits notifications on accept/reject/close                     │
├─────────────────────────────────────────────────────────────────────┤
│  open-files-manager.ts                                              │
│  ├── Tracks open files (max 10)                                     │
│  ├── Tracks cursor position + selected text                         │
│  └── Broadcasts changes via ide/contextUpdate notification          │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ HTTP + MCP Protocol
                                    │ (localhost:port)
                                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Gemini CLI (core)                           │
├─────────────────────────────────────────────────────────────────────┤
│  ide-client.ts                                                      │
│  ├── StreamableHTTPClientTransport or StdioClientTransport          │
│  ├── Discovers port via env vars or port file                       │
│  ├── Opens/closes diffs via MCP tool calls                          │
│  └── Receives ide/contextUpdate, ide/diffAccepted, etc.             │
└─────────────────────────────────────────────────────────────────────┘
```

---

## File-by-File Breakdown

### 1. `src/extension.ts` (234 lines)

**Purpose:** Main entry point - extension activation and deactivation lifecycle.

**Key Components:**

```typescript
// Constants
const CLI_IDE_COMPANION_IDENTIFIER = 'Google.gemini-cli-vscode-ide-companion';
const INFO_MESSAGE_SHOWN_KEY = 'geminiCliInfoMessageShown';
export const DIFF_SCHEME = 'gemini-diff';

// Managed surfaces (no update prompts)
const MANAGED_EXTENSION_SURFACES: ReadonlySet<IdeInfo['name']> = new Set([
  IDE_DEFINITIONS.firebasestudio.name,
  IDE_DEFINITIONS.cloudshell.name,
]);
```

**Activation Flow:**

1. Creates output channel for logging
2. Detects if running in a managed environment (Firebase Studio, Cloud Shell)
3. Checks for extension updates via VS Code Marketplace API
4. Initializes `DiffContentProvider` and `DiffManager`
5. Registers diff accept/cancel commands
6. Starts `IDEServer` (MCP server)
7. Shows first-time install message
8. Registers workspace folder change handlers
9. Registers "Run Gemini CLI" command

**VS Code APIs Used:**

| API | Purpose |
|-----|---------|
| `vscode.window.createOutputChannel()` | Logging |
| `vscode.workspace.onDidCloseTextDocument()` | Clean up diff views |
| `vscode.workspace.registerTextDocumentContentProvider()` | Virtual diff documents |
| `vscode.commands.registerCommand()` | Accept/Cancel diff, Run CLI, Show Notices |
| `vscode.workspace.onDidChangeWorkspaceFolders()` | Sync env vars |
| `vscode.workspace.onDidGrantWorkspaceTrust()` | Sync env vars |
| `vscode.window.createTerminal()` | Launch Gemini CLI |
| `vscode.window.showWorkspaceFolderPick()` | Multi-folder workspace support |
| `vscode.commands.executeCommand('setContext')` | Toggle diff toolbar visibility |
| `context.globalState` | Persist "info message shown" flag |
| `context.environmentVariableCollection` | Inject env vars into terminal |

**Update Check Pattern:**

```typescript
// Fetches from VS Code Marketplace API
const response = await fetch(
  'https://marketplace.visualstudio.com/_apis/public/gallery/extensionquery',
  {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Accept: 'application/json;api-version=7.1-preview.1',
    },
    body: JSON.stringify({
      filters: [{ criteria: [{ filterType: 7, value: CLI_IDE_COMPANION_IDENTIFIER }] }],
      flags: 946,  // IncludeVersions | IncludeFiles | etc.
    }),
  },
);

// Compare with semver
if (semver.gt(latestVersion, currentVersion)) {
  // Prompt user to update
}
```

---

### 2. `src/ide-server.ts` (479 lines)

**Purpose:** MCP server that the CLI connects to. Exposes tools and broadcasts IDE context.

**Key Components:**

```typescript
// Environment variables for CLI discovery
const IDE_SERVER_PORT_ENV_VAR = 'GEMINI_CLI_IDE_SERVER_PORT';
const IDE_WORKSPACE_PATH_ENV_VAR = 'GEMINI_CLI_IDE_WORKSPACE_PATH';
const IDE_AUTH_TOKEN_ENV_VAR = 'GEMINI_CLI_IDE_AUTH_TOKEN';
```

**Server Architecture:**

1. **Express HTTP Server** - Listens on `127.0.0.1:0` (random available port)
2. **MCP Protocol** - Uses `@modelcontextprotocol/sdk` with `StreamableHTTPServerTransport`
3. **Session Management** - Multiple CLI sessions can connect simultaneously

**Security Layers:**

```typescript
// 1. CORS - Only allow requests without Origin header (non-browser)
app.use(cors({
  origin: (origin, callback) => {
    if (!origin) return callback(null, true);
    return callback(new CORSError('Request denied by CORS policy.'), false);
  },
}));

// 2. Host header validation - Only localhost
app.use((req, res, next) => {
  const host = req.headers.host || '';
  const allowedHosts = [`localhost:${this.port}`, `127.0.0.1:${this.port}`];
  if (!allowedHosts.includes(host)) {
    return res.status(403).json({ error: 'Invalid Host header' });
  }
  next();
});

// 3. Bearer token auth
app.use((req, res, next) => {
  const authHeader = req.headers.authorization;
  if (!authHeader || parts[1] !== this.authToken) {
    res.status(401).send('Unauthorized');
    return;
  }
  next();
});
```

**Port Discovery Mechanism:**

```typescript
// Writes connection info to temp file + env vars
async function writePortAndWorkspace({ context, port, portFile, authToken }) {
  // 1. Set environment variables (injected into new terminals)
  context.environmentVariableCollection.replace('GEMINI_CLI_IDE_SERVER_PORT', port.toString());
  context.environmentVariableCollection.replace('GEMINI_CLI_IDE_WORKSPACE_PATH', workspacePath);
  context.environmentVariableCollection.replace('GEMINI_CLI_IDE_AUTH_TOKEN', authToken);

  // 2. Write port file for CLI discovery
  // Path: /tmp/gemini/ide/gemini-ide-server-{ppid}-{port}.json
  const content = JSON.stringify({ port, workspacePath, authToken });
  await fs.writeFile(portFile, content);
  await fs.chmod(portFile, 0o600);  // Secure permissions
}
```

**MCP Tools Registered:**

```typescript
server.registerTool('openDiff', {
  description: '(IDE Tool) Open a diff view to create or modify a file...',
  inputSchema: OpenDiffRequestSchema.shape,
}, async ({ filePath, newContent }) => {
  await diffManager.showDiff(filePath, newContent);
  return { content: [] };
});

server.registerTool('closeDiff', {
  description: '(IDE Tool) Close an open diff view for a specific file.',
  inputSchema: CloseDiffRequestSchema.shape,
}, async ({ filePath }) => {
  const content = await diffManager.closeDiff(filePath);
  return { content: [{ type: 'text', text: JSON.stringify({ content }) }] };
});
```

**Session Keep-Alive:**

```typescript
const keepAlive = setInterval(() => {
  transport.send({ jsonrpc: '2.0', method: 'ping' })
    .then(() => { missedPings = 0; })
    .catch(() => {
      missedPings++;
      if (missedPings >= 3) {
        clearInterval(keepAlive);
        // Session is dead
      }
    });
}, 60000); // 60 seconds
```

---

### 3. `src/diff-manager.ts` (257 lines)

**Purpose:** Manages diff views for showing proposed file changes.

**Key Components:**

```typescript
// Virtual document provider for "gemini-diff" scheme
export class DiffContentProvider implements vscode.TextDocumentContentProvider {
  private content = new Map<string, string>();
  private onDidChangeEmitter = new vscode.EventEmitter<vscode.Uri>();

  provideTextDocumentContent(uri: vscode.Uri): string {
    return this.content.get(uri.toString()) ?? '';
  }
}
```

**Diff View Creation:**

```typescript
async showDiff(filePath: string, newContent: string) {
  // Create virtual URI for new content
  const rightDocUri = vscode.Uri.from({
    scheme: DIFF_SCHEME,  // "gemini-diff"
    path: filePath,
    query: `rand=${Math.random()}`,  // Cache busting
  });

  // Set content in virtual document provider
  this.diffContentProvider.setContent(rightDocUri, newContent);

  // Determine left side (existing file or empty untitled)
  let leftDocUri;
  try {
    await vscode.workspace.fs.stat(fileUri);
    leftDocUri = fileUri;  // File exists
  } catch {
    leftDocUri = vscode.Uri.from({ scheme: 'untitled', path: filePath });  // New file
  }

  // Show diff view
  await vscode.commands.executeCommand('vscode.diff', leftDocUri, rightDocUri, `${path.basename(filePath)} ↔ Modified`, {
    preview: false,
    preserveFocus: true,
  });

  // Make right side editable
  await vscode.commands.executeCommand('workbench.action.files.setActiveEditorWriteableInSession');
}
```

**Diff Accept/Reject Flow:**

```typescript
// When user accepts diff
async acceptDiff(rightDocUri: vscode.Uri) {
  const diffInfo = this.diffDocuments.get(rightDocUri.toString());
  const rightDoc = await vscode.workspace.openTextDocument(rightDocUri);
  const modifiedContent = rightDoc.getText();  // May include user edits
  await this.closeDiffEditor(rightDocUri);

  // Emit notification to CLI
  this.onDidChangeEmitter.fire(IdeDiffAcceptedNotificationSchema.parse({
    jsonrpc: '2.0',
    method: 'ide/diffAccepted',
    params: { filePath: diffInfo.originalFilePath, content: modifiedContent },
  }));
}

// When user cancels diff
async cancelDiff(rightDocUri: vscode.Uri) {
  // ... similar but emits ide/diffRejected
}
```

**Tab Management:**

```typescript
private async closeDiffEditor(rightDocUri: vscode.Uri) {
  // Find and close the tab
  for (const tabGroup of vscode.window.tabGroups.all) {
    for (const tab of tabGroup.tabs) {
      const input = tab.input as { modified?: vscode.Uri };
      if (input?.modified?.toString() === rightDocUri.toString()) {
        await vscode.window.tabGroups.close(tab);
        return;
      }
    }
  }
}
```

---

### 4. `src/open-files-manager.ts` (182 lines)

**Purpose:** Tracks open files, cursor position, and selected text to provide context to CLI.

**Key Constants:**

```typescript
export const MAX_FILES = 10;
const MAX_SELECTED_TEXT_LENGTH = 16384; // 16 KiB limit
```

**State Structure (matches Zod schema in core):**

```typescript
interface IdeContext {
  workspaceState: {
    openFiles: Array<{
      path: string;           // Absolute file path
      timestamp: number;      // Last focused timestamp
      isActive: boolean;      // Currently focused
      selectedText?: string;  // Current selection
      cursor?: {
        line: number;         // 1-based
        character: number;    // 1-based
      };
    }>;
    isTrusted: boolean;       // Workspace trust status
  };
}
```

**Event Watchers:**

```typescript
constructor(private readonly context: vscode.ExtensionContext) {
  // Track active file changes
  vscode.window.onDidChangeActiveTextEditor((editor) => {
    if (editor && this.isFileUri(editor.document.uri)) {
      this.addOrMoveToFront(editor);
      this.fireWithDebounce();
    }
  });

  // Track cursor/selection changes
  vscode.window.onDidChangeTextEditorSelection((event) => {
    if (this.isFileUri(event.textEditor.document.uri)) {
      this.updateActiveContext(event.textEditor);
      this.fireWithDebounce();
    }
  });

  // Track file closes
  vscode.workspace.onDidCloseTextDocument((document) => { ... });

  // Track file deletes
  vscode.workspace.onDidDeleteFiles((event) => { ... });

  // Track file renames
  vscode.workspace.onDidRenameFiles((event) => { ... });
}
```

**Debounced Updates:**

```typescript
private fireWithDebounce() {
  if (this.debounceTimer) {
    clearTimeout(this.debounceTimer);
  }
  this.debounceTimer = setTimeout(() => {
    this.onDidChangeEmitter.fire();
  }, 50); // 50ms debounce
}
```

**File Filtering:**

```typescript
private isFileUri(uri: vscode.Uri): boolean {
  return uri.scheme === 'file';  // Ignore untitled, git, etc.
}
```

---

### 5. `src/utils/logger.ts` (25 lines)

**Purpose:** Conditional logging based on dev mode or config setting.

```typescript
export function createLogger(
  context: vscode.ExtensionContext,
  logger: vscode.OutputChannel,
) {
  return (message: string) => {
    const isDevMode = context.extensionMode === vscode.ExtensionMode.Development;
    const isLoggingEnabled = vscode.workspace
      .getConfiguration('gemini-cli.debug')
      .get('logging.enabled');

    if (isDevMode || isLoggingEnabled) {
      logger.appendLine(message);
    }
  };
}
```

---

## VS Code API Usage Patterns

### 1. Environment Variable Injection

VS Code allows extensions to inject environment variables into terminals:

```typescript
context.environmentVariableCollection.replace(
  'GEMINI_CLI_IDE_SERVER_PORT',
  port.toString(),
);
```

This is how the CLI discovers the server port without reading files.

### 2. Virtual Document Provider

For diff views, the extension creates "virtual" documents that don't exist on disk:

```typescript
vscode.workspace.registerTextDocumentContentProvider(
  'gemini-diff',  // Custom scheme
  diffContentProvider,
);
```

### 3. Context-Sensitive Commands

The extension uses `setContext` to show/hide toolbar buttons:

```typescript
await vscode.commands.executeCommand('setContext', 'gemini.diff.isVisible', true);
```

Combined with package.json `when` clauses:

```json
"menus": {
  "editor/title": [
    {
      "command": "gemini.diff.accept",
      "when": "gemini.diff.isVisible",
      "group": "navigation"
    }
  ]
}
```

### 4. Tab Group API

For closing specific diff tabs:

```typescript
for (const tabGroup of vscode.window.tabGroups.all) {
  for (const tab of tabGroup.tabs) {
    if (input?.modified?.toString() === rightDocUri.toString()) {
      await vscode.window.tabGroups.close(tab);
    }
  }
}
```

### 5. Global State Persistence

```typescript
// Check if already shown
context.globalState.get(INFO_MESSAGE_SHOWN_KEY);

// Persist across sessions
context.globalState.update(INFO_MESSAGE_SHOWN_KEY, true);
```

---

## Integration Points with CLI

### 1. Discovery Mechanism

The CLI discovers the extension via multiple fallback methods:

| Method | How |
|--------|-----|
| **Environment Variables** | `GEMINI_CLI_IDE_SERVER_PORT`, `GEMINI_CLI_IDE_AUTH_TOKEN`, `GEMINI_CLI_IDE_WORKSPACE_PATH` injected into new terminals |
| **Port File** | `/tmp/gemini/ide/gemini-ide-server-{ppid}-{port}.json` for existing terminals |
| **Stdio Transport** | Alternative for non-HTTP connections (via `GEMINI_CLI_IDE_SERVER_STDIO_COMMAND`) |

### 2. MCP Protocol Messages

**Extension -> CLI (Notifications):**

| Notification | When |
|--------------|------|
| `ide/contextUpdate` | Open files, cursor, selection changes |
| `ide/diffAccepted` | User accepts a diff (includes final content) |
| `ide/diffRejected` | User rejects/closes a diff |
| `ping` | Keep-alive every 60s |

**CLI -> Extension (Tool Calls):**

| Tool | Purpose |
|------|---------|
| `openDiff` | Show diff view for a file |
| `closeDiff` | Programmatically close a diff |

### 3. Workspace Validation

The CLI validates that it's running within the IDE's open workspace:

```typescript
// From ide-client.ts
static validateWorkspacePath(ideWorkspacePath, cwd) {
  // Check if CLI's cwd is within any of the IDE's open workspace folders
  const isWithinWorkspace = ideWorkspacePaths.some((workspacePath) =>
    isSubpath(workspacePath, realCwd),
  );

  if (!isWithinWorkspace) {
    return { isValid: false, error: 'Directory mismatch...' };
  }
}
```

### 4. Diff Mutex

The CLI uses a mutex to ensure only one diff is open at a time:

```typescript
// From ide-client.ts
async openDiff(filePath: string, newContent: string) {
  const release = await this.acquireMutex();
  // ... open diff and wait for resolution
  promise.finally(release);
  return promise;
}
```

---

## Package Configuration

### package.json Highlights

```json
{
  "name": "gemini-cli-vscode-ide-companion",
  "publisher": "google",
  "engines": { "vscode": "^1.99.0" },
  "activationEvents": ["onStartupFinished"],
  "contributes": {
    "configuration": {
      "properties": {
        "gemini-cli.debug.logging.enabled": {
          "type": "boolean",
          "default": false
        }
      }
    },
    "commands": [
      { "command": "gemini.diff.accept", "title": "Gemini CLI: Accept Diff", "icon": "$(check)" },
      { "command": "gemini.diff.cancel", "title": "Gemini CLI: Close Diff Editor", "icon": "$(close)" },
      { "command": "gemini-cli.runGeminiCLI", "title": "Gemini CLI: Run" }
    ],
    "keybindings": [
      { "command": "gemini.diff.accept", "key": "ctrl+s", "when": "gemini.diff.isVisible" },
      { "command": "gemini.diff.accept", "key": "cmd+s", "when": "gemini.diff.isVisible" }
    ]
  },
  "dependencies": {
    "@modelcontextprotocol/sdk": "^1.23.0",
    "cors": "^2.8.5",
    "express": "^5.1.0",
    "zod": "^3.25.76"
  }
}
```

**Key Design Decisions:**

1. **`onStartupFinished`** - Extension activates after VS Code fully loads (non-blocking)
2. **Ctrl/Cmd+S to Accept** - Familiar "save" gesture accepts the diff
3. **Express 5** - Modern HTTP server framework
4. **MCP SDK** - Official Model Context Protocol implementation

---

## Key Takeaways

### 1. MCP as the Integration Protocol

Using MCP (Model Context Protocol) provides:
- Standardized tool registration and discovery
- Bidirectional notifications
- Session management with keep-alive
- Compatible with other AI tools

### 2. Security-First Design

Multiple layers of protection:
- Bearer token authentication (randomly generated per session)
- Host header validation (only localhost)
- CORS rejection (no browser access)
- File permissions (0o600 on port file)
- Workspace path validation (CLI must be within open folders)

### 3. Graceful Discovery

Fallback chain for CLI to find extension:
1. Environment variables (new terminals)
2. Port file lookup (existing terminals)
3. Stdio transport (alternative)

### 4. Virtual Document Pattern

Using `TextDocumentContentProvider` for diff views:
- No temp files on disk
- Full VS Code diff integration
- Supports user edits before accept

### 5. Event-Driven Context Updates

Debounced (50ms) updates for:
- File opens/closes/renames/deletes
- Cursor position changes
- Text selection changes
- Workspace trust changes

### 6. Diff Mutex Pattern

Single diff at a time prevents:
- Race conditions
- UI confusion
- Resource conflicts

### 7. Managed Environment Detection

Special handling for Firebase Studio, Cloud Shell, etc.:
- No update prompts
- No install messages
- Extension is pre-installed

---

## Implications for AVA

For AVA's VS Code extension implementation, consider:

1. **Use MCP** - Provides standardized tooling and is becoming an industry standard
2. **Environment Variable Injection** - Most reliable discovery mechanism
3. **Virtual Documents** - Better than temp files for diff views
4. **Security Layers** - Bearer tokens + host validation + CORS
5. **Context Awareness** - Track open files, cursor, selection
6. **Debouncing** - Prevent excessive updates on rapid changes
7. **Managed Environments** - Detect cloud IDEs that pre-install extensions
8. **Keep-Alive** - Handle connection drops gracefully
9. **Workspace Validation** - Ensure CLI is in the right directory
