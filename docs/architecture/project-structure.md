# Project Structure

> Complete file organization for the Tauri + SolidJS application

---

```
project/
├── src/                          # Frontend (SolidJS + TypeScript)
│   ├── App.tsx                   # Root component
│   ├── index.tsx                 # Entry point
│   ├── index.css                 # Global styles + Tailwind
│   │
│   ├── components/               # UI Components
│   │   ├── layout/
│   │   │   ├── AppShell.tsx      # Main app container
│   │   │   ├── Sidebar.tsx       # Project/session navigation
│   │   │   ├── TabBar.tsx        # Multi-tab support
│   │   │   └── StatusBar.tsx     # Agent status indicators
│   │   │
│   │   ├── chat/
│   │   │   ├── ChatWindow.tsx    # Main conversation area
│   │   │   ├── MessageBubble.tsx # Individual messages
│   │   │   ├── StreamingText.tsx # Real-time text streaming
│   │   │   ├── CodeBlock.tsx     # Syntax-highlighted code
│   │   │   └── InputArea.tsx     # User input with commands
│   │   │
│   │   ├── agents/
│   │   │   ├── AgentPanel.tsx    # Shows active agents
│   │   │   ├── AgentCard.tsx     # Individual agent status
│   │   │   ├── CommanderView.tsx # Commander's plan/backlog
│   │   │   └── OperatorList.tsx  # Active operators grid
│   │   │
│   │   ├── editor/
│   │   │   ├── FileTree.tsx      # Project file browser
│   │   │   ├── DiffViewer.tsx    # Show file changes
│   │   │   ├── InlineEdit.tsx    # Inline code editing
│   │   │   └── Terminal.tsx      # Embedded terminal
│   │   │
│   │   └── common/
│   │       ├── Button.tsx
│   │       ├── Modal.tsx
│   │       ├── Tooltip.tsx
│   │       └── LoadingSpinner.tsx
│   │
│   ├── stores/                   # State Management
│   │   ├── agentStore.ts         # Agent states (Commander, Operators)
│   │   ├── sessionStore.ts       # Current session data
│   │   ├── projectStore.ts       # Active project/workspace
│   │   ├── chatStore.ts          # Conversation history
│   │   └── settingsStore.ts      # User preferences
│   │
│   ├── services/                 # Business Logic
│   │   ├── agents/
│   │   │   ├── commanderService.ts   # Commander orchestration
│   │   │   ├── operatorService.ts    # Operator task execution
│   │   │   └── agentFactory.ts       # Spawn new agents
│   │   │
│   │   ├── llm/
│   │   │   ├── providerManager.ts    # Multi-provider support
│   │   │   ├── anthropicClient.ts    # Claude API
│   │   │   ├── openaiClient.ts       # OpenAI API
│   │   │   ├── googleClient.ts       # Gemini API
│   │   │   └── streamingHandler.ts   # Handle SSE streams
│   │   │
│   │   ├── tools/
│   │   │   ├── fileEdit.ts           # str_replace, create_file
│   │   │   ├── fileRead.ts           # Read file contents
│   │   │   ├── bash.ts               # Execute shell commands
│   │   │   ├── search.ts             # Grep/ripgrep wrapper
│   │   │   └── lspBridge.ts          # LSP tool calls
│   │   │
│   │   └── documentation/
│   │       ├── docManager.ts         # Manage /docs folder
│   │       ├── docGenerator.ts       # Auto-generate docs
│   │       └── contextCompressor.ts  # Pre-compaction summarizer
│   │
│   ├── hooks/                    # SolidJS Hooks
│   │   ├── useAgent.ts           # Agent lifecycle
│   │   ├── useChat.ts            # Chat operations
│   │   ├── useProject.ts         # Project operations
│   │   └── useKeyboard.ts        # Keyboard shortcuts
│   │
│   ├── utils/
│   │   ├── tauri.ts              # Tauri IPC helpers
│   │   ├── formatters.ts         # Code/text formatting
│   │   └── validators.ts         # Input validation
│   │
│   └── types/
│       ├── agent.ts              # Agent type definitions
│       ├── message.ts            # Message types
│       ├── tool.ts               # Tool definitions
│       └── project.ts            # Project types
│
├── src-tauri/                    # Backend (Rust)
│   ├── Cargo.toml                # Rust dependencies
│   ├── tauri.conf.json           # Tauri configuration
│   ├── capabilities/
│   │   └── default.json          # Permissions
│   │
│   ├── src/
│   │   ├── main.rs               # Entry point
│   │   ├── lib.rs                # Library root
│   │   │
│   │   ├── commands/             # Tauri Commands (IPC)
│   │   │   ├── mod.rs
│   │   │   ├── file_ops.rs       # File operations
│   │   │   ├── shell.rs          # Shell execution
│   │   │   ├── project.rs        # Project management
│   │   │   └── agent.rs          # Agent management
│   │   │
│   │   ├── lsp/                  # LSP Integration
│   │   │   ├── mod.rs
│   │   │   ├── client.rs         # LSP client implementation
│   │   │   ├── manager.rs        # Multi-language LSP manager
│   │   │   └── watcher.rs        # File watcher for LSP sync
│   │   │
│   │   ├── tools/                # Tool Implementations
│   │   │   ├── mod.rs
│   │   │   ├── str_replace.rs    # String replacement tool
│   │   │   ├── file_create.rs    # File creation
│   │   │   ├── bash.rs           # Bash execution
│   │   │   └── diagnostics.rs    # LSP diagnostics tool
│   │   │
│   │   ├── db/                   # Database Layer
│   │   │   ├── mod.rs
│   │   │   ├── migrations.rs     # SQLite migrations
│   │   │   ├── sessions.rs       # Session CRUD
│   │   │   ├── messages.rs       # Message history
│   │   │   └── agents.rs         # Agent state persistence
│   │   │
│   │   └── utils/
│   │       ├── mod.rs
│   │       ├── paths.rs          # Path resolution
│   │       └── git.rs            # Git operations
│   │
│   └── icons/                    # App icons
│
├── docs/                         # Project Documentation
│   ├── VISION.md                 # Project vision and roadmap
│   ├── architecture/             # System design
│   ├── agents/                   # Agent specifications
│   ├── development/              # Dev guides
│   └── reference/                # API reference
│
├── package.json
├── tsconfig.json
├── tailwind.config.js
├── vite.config.ts
└── README.md
```
