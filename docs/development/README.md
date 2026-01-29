# Development

> Guides for developing and contributing

---

## Documents

| Document | Description |
|----------|-------------|
| [setup.md](./setup.md) | Development environment setup |
| [conventions.md](./conventions.md) | Coding conventions and patterns |
| [testing.md](./testing.md) | Testing strategy |

---

## Quick Start

```bash
# Create the project
npm create tauri-app@latest [project-name] -- --template solid-ts

# Navigate and install
cd [project-name]
npm install

# Add dependencies
npm install -D tailwindcss postcss autoprefixer
npm install zustand @solidjs/router

# Initialize Tailwind
npx tailwindcss init -p

# Add Tauri plugins (in src-tauri/)
cd src-tauri
cargo add tauri-plugin-sql --features sqlite
cargo add tauri-plugin-shell
cargo add tauri-plugin-fs
cargo add tokio --features full
cargo add serde --features derive
cargo add serde_json

# Run development
npm run tauri dev
```

---

## Key Files to Create First

1. **`src-tauri/src/commands/file_ops.rs`** - Core file editing
2. **`src/services/llm/streamingHandler.ts`** - LLM streaming
3. **`src/stores/agentStore.ts`** - Agent state
4. **`src/components/chat/StreamingText.tsx`** - Real-time text
5. **`src/services/agents/commanderService.ts`** - Commander logic
