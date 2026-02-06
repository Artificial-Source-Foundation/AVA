# Estela — Vision

> The Obsidian of AI Coding

---

## The Pitch

Estela is a **desktop AI coding app** for developers and vibe coders. Think Claude Code meets Obsidian — simple to use, open source, multi-provider, with a community plugin ecosystem where anyone can build and share their workflows.

It's not an IDE replacement. It's the AI companion that makes your IDE better.

---

## Who Is It For

**Experienced Developers** who want:
- Model flexibility — use Claude for reasoning, GPT for edits, Gemini for large context, local models for privacy
- A clean agent workflow that shows what's happening, not a black box
- Community plugins for their specific stack and workflows

**Vibe Coders** (AI-first developers) who want:
- Chat and go — describe what you want, AI builds it
- Simple UI that doesn't overwhelm
- One-click plugins to add capabilities without coding

**Plugin Creators** who want:
- A dead-simple SDK to build plugins
- An audience that can discover and install their work
- Vibe-code their own plugins and workflows

---

## What Makes Estela Special

### 1. The Dev Team

Your AI isn't a single chatbot. It's a **virtual development team** you can see working.

```
┌─────────────────────────────────────────────────┐
│  TEAM LEAD (main chat)                          │
│  "I'll split this into frontend and backend..." │
│  "Assigning Senior Frontend Lead..."            │
├────────────────────┬────────────────────────────┤
│  Senior Frontend   │  Senior Backend            │
│  Lead              │  Lead                      │
│  ├─ Jr. Dev: UI    │  ├─ Jr. Dev: API routes    │
│  │  [working...]   │  │  [done ✓]               │
│  └─ Jr. Dev: CSS   │  └─ Jr. Dev: Database      │
│     [done ✓]       │     [working...]           │
│                    │                            │
│  [Chat with team]  │  [Chat with team]          │
└────────────────────┴────────────────────────────┘
```

**How it works:**
- **Team Lead** — Plans, delegates, coordinates. Uses the smartest model.
- **Senior Leads** — Domain specialists (frontend, backend, testing, etc.). Each leads a group of workers.
- **Junior Devs** — Execute specific file-level tasks. Use cheaper/faster models.

**User control:**
- Workers auto-report to their Senior Lead when done (default)
- Senior Leads auto-report to Team Lead when their group is done
- User can **click into any agent's chat** to talk to them directly
- User can fix issues, give extra instructions, then send results back up
- Full visibility into what every team member is doing

### 2. Multi-Provider

Not locked into one AI vendor. Use the best model for each job.

| Task | Best Model | Why |
|------|-----------|-----|
| Planning | Claude Opus | Best reasoning |
| Code edits | GPT-4 | Fast, accurate edits |
| Large context | Gemini | 1M+ token window |
| Privacy | Ollama (local) | Never leaves your machine |
| Speed | Groq | Fastest inference |

12+ providers built in: Anthropic, OpenAI, Google, Mistral, Groq, DeepSeek, xAI, Cohere, Together, Ollama, OpenRouter, and more.

### 3. Obsidian-Style Plugins

**Easy to find:**
- Built-in plugin marketplace
- Browse by category, search, one-click install
- Community ratings and downloads

**Easy to create:**
- Simple markdown + config format
- Vibe-code your own plugins — describe what you want, AI builds the plugin
- Two types:
  - **Skills** — Auto-invoked based on context (file patterns, project type)
  - **Commands** — Manually invoked with `/slash`
- Plugins can bundle skills + commands + hooks + MCP servers

**Easy to share:**
- Publish to the marketplace from your repo
- Namespace prevents conflicts (`my-plugin:command-name`)

### 4. Desktop-Native

Built with Tauri (Rust backend + SolidJS frontend):
- ~5MB app size (not 200MB like Electron)
- 30MB RAM idle (not 300MB)
- Sub-500ms startup
- Local-first — your data stays on your machine
- Lightweight code viewer built in for quick reads

### 5. Simple by Default, Powerful When Needed

**For vibe coders:** Clean chat interface → type what you want → watch the team work → done.

**For power users:** Hooks, policies, custom commands, trusted folders, extensions, plan mode, multiple agent configurations.

---

## Design Philosophy

### UI Inspiration
- **Obsidian** — Simple, extensible, community-driven
- **Arc** — Minimal chrome, beautiful defaults
- **Vercel** — Premium feel, clean typography
- **Cursor** — IDE integration done right
- **Warp** — Modern terminal aesthetic

### Principles
- **Minimalistic and premium** — Every pixel earns its place
- **Progressive disclosure** — Simple surface, power underneath
- **Community-first** — Built by developers, loved by developers, used by everyone
- **Open source** — MIT license, transparent development

---

## Platform Priority

```
Priority 1: Desktop App (Tauri)     ← THIS IS ESTELA
Priority 2: Plugin Ecosystem        ← THE DIFFERENTIATOR
Priority 3: CLI                     ← Secondary interface
Priority 4: Editor Integration      ← VS Code/Cursor backend (ACP)
Priority 5: Agent Network           ← Remote agent calls (A2A)
```

---

## Roadmap

### Phase 1: Desktop App (Current Priority)
- [ ] Working Tauri desktop app with chat + dev team UI
- [ ] Team Lead → Senior Leads → Junior Devs visible in UI
- [ ] Agent cards with progress, expand to see work
- [ ] Click-to-chat with any team member
- [ ] Lightweight code viewer
- [ ] Settings, session management, provider configuration

### Phase 2: Plugin Ecosystem
- [ ] Unified plugin format (skills + commands + hooks)
- [ ] Plugin SDK — dead simple to create
- [ ] Built-in marketplace UI
- [ ] Publish from GitHub repos
- [ ] Community ratings, search, categories

### Phase 3: Polish & Community
- [ ] CLI interface (secondary)
- [ ] Plugin creation wizard (vibe-code your plugins)
- [ ] Community templates and starter plugins
- [ ] Documentation site

### Phase 4: Integrations
- [ ] Editor integration (ACP — use Estela as VS Code backend)
- [ ] Agent network (A2A — connect to remote agents)
- [ ] Voice input
- [ ] Vision models (image understanding)

---

## The Name

**Estela** — Spanish for "star trail" or "wake" (the trail a ship leaves).

Your AI team leaves a trail of completed work behind it.
