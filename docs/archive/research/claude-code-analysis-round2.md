# Claude Code Analysis — Round 2

> Historical research document from Sprint 61 (2026-03-31). This pre-dates the AVA 3.3 reset and should not be treated as current roadmap or architecture guidance.

Date: 2026-03-31 | Agents: 9 parallel scrapers | Source: `docs/reference-code/claude-code/`

## Executive Summary

Round 1 (Sprint 61) identified and implemented 18 features. Round 2 scraped 9 areas in depth and found **60+ additional features** across 10 categories. The biggest gaps are in multi-agent coordination, security hardening, MCP depth, prompt engineering, and extensibility.

---

## Category 1: Multi-Agent / Swarm Architecture

### What CC Has

| Feature | Description | Files |
|---------|-------------|-------|
| **Agent Swarms/Teammates** | Multi-process parallel agents with tmux/iTerm2/in-process backends | `src/utils/swarm/`, `src/utils/teammate.ts` |
| **File-Based Mailbox** | `.claude/teams/{name}/inboxes/{agent}.json` with advisory locking | `src/utils/teammateMailbox.ts` |
| **AsyncLocalStorage Isolation** | Each teammate gets isolated async context in same process | `src/utils/teammateContext.ts` |
| **Team File** | `.claude/teams/{name}/team.json` tracks all members, sessions, permissions | `src/utils/swarm/` |
| **SendMessage Tool** | Inter-agent messaging with named addressing | `src/tools/SendMessageTool/` |
| **TeamCreate/TeamDelete** | Team lifecycle management | `src/tools/TeamCreateTool/` |
| **Dream Agent** | Background memory consolidation (`starting` → `updating`) | `src/tasks/DreamTask/` |
| **Coordinator Mode** | Orchestrates multiple agents with custom system prompt | `src/coordinator/` |
| **Sidechain Transcripts** | Per-agent JSONL at `.claude/sessions/<id>/agents/<agentId>.jsonl` | `src/utils/sessionStorage.ts` |
| **CacheSafeParams Sharing** | Forked agents share parent's prompt cache (90% cost reduction) | `src/utils/forkedAgent.ts` |
| **3 Teammate Backends** | Tmux panes, iTerm2 splits, in-process (fallback) | `src/utils/swarm/backends/` |

### AVA Gap

AVA has HQ (Director→Leads→Workers) but lacks:
- File-based mailbox for cross-process coordination
- AsyncLocalStorage-style isolation for in-process agents
- Dream agent for background memory consolidation
- CacheSafeParams prompt cache sharing between parent/child
- Tmux/iTerm2 pane backends

### Priority: HIGH (aligns with HQ roadmap)

---

## Category 2: Security Hardening

### What CC Has Beyond AVA

| Feature | Description | Files |
|---------|-------------|-------|
| **24 Bash Security Checks** | vs AVA's ~22 — adds: JQ RCE, obfuscated flags, newline injection, proc environ, comment/quote desync, backslash-escaped operators, quoted newlines, malformed tokens, mid-word hash | `src/tools/BashTool/bashSecurity.ts` |
| **SSRF Guard** | Blocks cloud metadata IPs (169.254.0.0/16, 100.64.0.0/10), private ranges, IPv4-mapped IPv6 | `src/utils/hooks/ssrfGuard.ts` |
| **Unicode Sanitization** | NFKC normalization + removal of Cf/Co/Cn categories, prevents ASCII smuggling via Unicode tags | `src/utils/sanitization.ts` |
| **Secret Scanner** | 30+ patterns: AWS, GCP, Azure, Anthropic, OpenAI, GitHub, GitLab, Slack, Twilio, SendGrid, npm, pip | `src/services/teamMemorySync/secretScanner.ts` |
| **ML Permission Classifier** | 2-stage XML thinking for auto-approval decisions with confidence levels | `src/utils/permissions/yoloClassifier.ts` |
| **Server-Side Rate Limiting** | Sliding window per-IP: 1000 global, 5 auth, 30 msg, 100 file, 20 cmd per min | `src/server/security/rate-limiter.ts` |
| **Audit Logging** | Structured JSON with auto-redaction of secrets, 10 event types | `src/server/security/audit-log.ts` |
| **Server Command Sandbox** | Allowlist/denylist + env stripping (ANTHROPIC_API_KEY, DATABASE_URL, JWT_SECRET, etc.) + execFile (no shell) | `src/server/security/command-sandbox.ts` |
| **Dangerous Removal Path Detection** | Blocks rm -rf on /, /bin, /usr, /etc, /home, C:\Windows, etc. | `src/utils/permissions/pathValidation.ts` |
| **Shadowed Rule Detection** | Identifies overly-broad permission rules that shadow specific ones | `src/utils/permissions/shadowedRuleDetection.ts` |
| **Denial Tracking & Fallback** | Tracks successive classifier denials, falls back to prompting | `src/utils/permissions/denialTracking.ts` |
| **Feature Gating for Dangerous Modes** | `bypassPermissions` disabled org-wide via Statsig | `src/utils/permissions/bypassPermissionsKillswitch.ts` |

### AVA Gap

AVA has F9 (5 patterns) + F12 (17 injection patterns) but lacks:
- SSRF guard for web_fetch
- Unicode sanitization (ASCII smuggling defense)
- Secret scanner for credentials in output
- Server-side rate limiting (web mode)
- Audit logging with redaction
- Dangerous removal path detection
- Shadowed rule detection

### Priority: HIGH (security is table stakes)

---

## Category 3: MCP Depth

### What CC Has Beyond AVA

| Feature | Description | Files |
|---------|-------------|-------|
| **6 Transports** | stdio, SSE, HTTP/streamable, WebSocket, SDK control, in-process | `src/services/mcp/client.ts` |
| **Elicitation System** | Two-phase URL approval + form mode + programmatic hooks | `src/services/mcp/elicitationHandler.ts` |
| **Channel Notifications** | Server→user messages (Discord, Slack, SMS) via XML wrapping | `src/services/mcp/channelNotification.ts` |
| **OAuth/XAA Enterprise Auth** | RFC 8693 token exchange + RFC 7523 JWT bearer + Keychain storage | `src/services/mcp/auth.ts`, `xaa.ts` |
| **Binary Blob Handling** | Persists base64 blobs to disk, returns path reference | `src/services/mcp/client.ts` |
| **MCPB Packages** | Zip-based compiled MCP packages with DXT manifests | `src/utils/plugins/mcpbHandler.ts` |
| **Resource Templates** | Server-side resource template discovery | `src/services/mcp/client.ts` |
| **MCP Prompts as Commands** | Converts MCP prompts to slash commands with argument parsing | `src/services/mcp/client.ts` |
| **Connection Drop Detection** | 3+ consecutive terminal errors → auto-reconnect | `src/services/mcp/client.ts` |
| **Max Description Length** | Caps at 2048 chars to prevent token waste from OpenAPI servers | `src/services/mcp/client.ts` |
| **Result Token Validation** | Configurable max output tokens per MCP call | `src/services/mcp/mcpValidation.ts` |

### AVA Gap

AVA has basic MCP (stdio, tool list/call, batched refresh from F13) but lacks:
- HTTP/WebSocket/SSE transports
- Elicitation flows
- Channel notifications
- OAuth/enterprise auth
- Binary blob persistence
- Connection drop detection with auto-reconnect
- Result token validation

### Priority: MEDIUM (most users use stdio MCP)

---

## Category 4: Prompt Engineering

### What CC Has Beyond AVA

| Feature | Description | Files |
|---------|-------------|-------|
| **Two-Tier Section Caching** | `systemPromptSection()` for cached, `DANGEROUS_uncachedSection()` for volatile | `src/constants/systemPromptSections.ts` |
| **Dynamic Boundary Marker** | `__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__` splits global vs org cache scope | `src/constants/systemPromptSections.ts` |
| **TTL Selection** | 5-min default, 1-hour for qualified users/sources | `src/services/api/promptCacheBreakDetection.ts` |
| **Sticky State Latching** | AFK/fast mode headers latched once set, prevents cache thrashing | `src/services/api/promptCacheBreakDetection.ts` |
| **Attachment Deltas** | Only announce changed MCP servers, skills, memories per turn | `src/utils/mcpInstructionsDelta.ts` |
| **Semantic Memory Surfacing** | Up to 5 files/turn, 4KB each, 60KB session budget, based on relevance | `src/memdir/memdir.ts` |
| **Cache Break Detection** | Hash 15+ parameters, log breaks with diffs, 2K token threshold | `src/services/api/promptCacheBreakDetection.ts` |
| **@include Directive** | `@path` syntax in CLAUDE.md for file inclusion with circular prevention | `src/utils/claudemd.ts` |
| **Modular Prompt Arrays** | System prompt built as string arrays, conditionally composed | `src/constants/prompts.ts` |
| **CLAUDE.local.md** | Private project-specific instructions (not checked in) | `src/utils/claudemd.ts` |

### AVA Gap

AVA has F2 (cache boundary) and instruction file loading but lacks:
- Two-tier caching with dynamic boundary
- TTL selection for different cache lifetimes
- Sticky state latching
- Attachment deltas (only send changes)
- Semantic memory surfacing with budget
- Cache break detection with logging
- @include directive in instruction files
- CLAUDE.local.md (private instructions)

### Priority: HIGH (direct cost savings via caching)

---

## Category 5: Hooks & Extensibility

### What CC Has Beyond AVA

| Feature | Description | Files |
|---------|-------------|-------|
| **6 Hook Types** | command, prompt, http, agent, function, callback | `src/utils/hooks/` |
| **10+ Hook Events** | SessionStart, PreToolUse, PostToolUse, PostToolUseFailure, UserPromptSubmit, Stop, PermissionRequest, Notification, FileChanged, CwdChanged | `src/types/hooks.ts` |
| **Multi-Source Hooks** | user, project, policy, plugin, session, builtin sources | `src/utils/hooks/hooksSettings.ts` |
| **Plugin System** | Installable packages with commands, agents, skills, hooks, MCP, output styles, LSP | `src/plugins/`, `src/services/plugins/` |
| **Plugin Marketplace** | Git-based install, auto-update, version pinning | `src/utils/plugins/marketplaceManager.ts` |
| **16 Bundled Skills** | batch, debug, loop, simplify, skillify, stuck, verify, etc. | `src/skills/bundledSkills.ts` |
| **3 Command Types** | PromptCommand, LocalCommand, LocalJSXCommand | `src/types/command.ts` |
| **Skill Forking** | Skills run inline or fork as sub-agents with isolated token budget | `src/skills/` |
| **MCP Skills** | Create skills from MCP `skill://` resource URIs | `src/skills/mcpSkillBuilders.ts` |
| **Enterprise Lockdown** | `strictPluginOnlyCustomization` restricts skills/agents/hooks/mcp | `src/utils/settings/types.ts` |

### AVA Gap

AVA has TOML custom tools and plugin system but lacks:
- HTTP/agent/prompt hook types (only has command hooks via ava-extensions)
- Plugin marketplace with auto-update
- Bundled skills library
- Skill forking (inline vs sub-agent)
- MCP-based skill discovery
- Enterprise lockdown surfaces

### Priority: MEDIUM (extensibility is differentiator)

---

## Category 6: Tool Sophistication

### What CC Has Beyond AVA

| Feature | Description | Files |
|---------|-------------|-------|
| **39 Tools Total** | vs AVA's 9 default + 7 extended | `src/tools/` |
| **StreamingToolExecutor** | Concurrent execution with backpressure and status tracking | `src/services/tools/StreamingToolExecutor.ts` |
| **Tool Result Persistence** | >50KB results written to disk with inline preview | Tool execution |
| **Search/Read Classification** | Collapsible search (find/grep/rg), read (cat/head), list (ls/tree) | `src/tools/BashTool/` |
| **Semantic Input Parsing** | `semanticNumber`, `semanticBoolean` for flexible LLM input | Tool schemas |
| **searchHint** | 3-10 word capability phrases for ToolSearch matching | Tool definitions |
| **Content Replacement Budget** | Aggregate tool result truncation across conversation | Tool execution |
| **Input Backfill** | `backfillObservableInput()` adds legacy/derived fields before hooks | Tool framework |
| **Tool Grouping** | `renderGroupedToolUse()` for parallel tool visualization | AgentTool, SearchTools |
| **isConcurrencySafe** | Per-tool declaration of thread safety (fail-closed default) | Tool framework |
| **isDestructive** | Marks irreversible operations for extra confirmation | Tool framework |
| **Image Processing** | Resize, downsample, format detection for FileRead | `src/tools/FileReadTool/` |
| **PDF Extraction** | Page range support with token limits | `src/tools/FileReadTool/` |
| **Jupyter Notebooks** | Cell-level reading and editing | `src/tools/NotebookEditTool/` |
| **LSP Tool** | Direct language server protocol queries | `src/tools/LSPTool/` |
| **Worktree Tools** | Enter/exit git worktree for isolated work | `src/tools/EnterWorktreeTool/` |
| **Cron Tools** | Schedule recurring tasks | `src/tools/ScheduleCronTool/` |
| **PowerShell Tool** | Windows-specific shell execution | `src/tools/PowerShellTool/` |
| **REPL Tool** | Multi-language REPL with primitive tool delegation | `src/tools/REPLTool/` |

### Priority: MEDIUM (most critical tools already exist in AVA)

---

## Category 7: LLM / Streaming Patterns

### What CC Has Beyond AVA

| Feature | Description | Files |
|---------|-------------|-------|
| **Persistent Retry Mode** | 5-min max backoff + 6-hour reset cap for unattended sessions | `src/services/api/withRetry.ts` |
| **Fallback Routing** | Opus→Sonnet on 3 consecutive 529 errors | `src/services/api/withRetry.ts` |
| **Heartbeat Messages** | Every 30s during long waits (rate limit, retry) | `src/services/api/withRetry.ts` |
| **Gateway Detection** | 7 AI gateway fingerprints (litellm, helicone, portkey, etc.) | `src/services/api/errors.ts` |
| **Fast Mode Pricing** | Separate pricing tier: $30/$150 vs $5/$25 (Opus) | `src/utils/modelCost.ts` |
| **ISP Support** | Interleaved Streaming Processing on Claude 4+ | `src/utils/thinking.ts` |
| **Per-Request Client ID** | UUID `client-request-id` for server correlation | `src/services/api/client.ts` |
| **Token Gap Calculation** | Regex parsing of "prompt too long" errors to compute gap | `src/services/api/errors.ts` |
| **Continuation Detection** | Diminishing returns detection on continuation queries | `src/query/tokenBudget.ts` |

### Priority: LOW-MEDIUM (AVA has solid retry/backoff already)

---

## Category 8: UI/UX Patterns

### What CC Has Beyond AVA

| Feature | Description | Files |
|---------|-------------|-------|
| **Virtual Scrolling** | Height-aware virtual scroll for long message lists | `src/components/VirtualMessageList.tsx` |
| **Unified Suggestions** | Fuse.js fuzzy matching across files, MCP resources, agents (max 15) | `src/hooks/unifiedSuggestions.ts` |
| **Stall Detection** | Spinner detects tool execution stalls with visual feedback | `src/components/Spinner/` |
| **Shell History Completion** | Autocomplete from shell history | `src/utils/suggestions/shellHistoryCompletion.ts` |
| **Bash Mode (! prefix)** | `!` prefix in input for direct bash execution | `src/components/PromptInput/inputModes.ts` |
| **Agent Color Manager** | Per-agent color coding in UI | `src/tools/AgentTool/agentColorManager.ts` |
| **Transcript Search** | Regex search through conversation history | `src/components/HistorySearchDialog.tsx` |
| **Priority Notifications** | Low/medium/high priority with timeout management | `src/context/notifications.js` |
| **Rate Limit Display** | 5-hour and 7-day window status in status line | `src/components/StatusLine.tsx` |
| **Progress Bars** | Unicode fractional block characters (▏▎▍▌▋▊▉█) | `src/components/design-system/ProgressBar.tsx` |

### Priority: LOW (nice-to-have UX polish)

---

## Category 9: Novel Features

### What CC Has That AVA Doesn't At All

| Feature | Description | Complexity | Priority |
|---------|-------------|------------|----------|
| **Computer Use (Chicago)** | Screen control, mouse, keyboard, app interaction | Huge | LOW (niche) |
| **Teleport** | Remote session execution on different machines | Large | LOW |
| **Voice Integration** | Speech-to-text streaming input | Medium | LOW |
| **IDE Bridge** | VS Code/JetBrains bidirectional communication | Large | MEDIUM |
| **Desktop/Mobile Handoff** | Seamless session transfer | Small | LOW |
| **Deep Links** | Protocol handler for out-of-band communication | Small | LOW |
| **Thinkback** | Session replay and thinking review | Small | LOW |
| **x402 Payment Protocol** | Payment protocol support | Small | LOW |

---

## Category 10: Configuration & Enterprise

### What CC Has Beyond AVA

| Feature | Description |
|---------|-------------|
| **Managed Settings** | Remote sync for enterprise organizations |
| **MDM Integration** | macOS Keychain + MDM, Windows Registry for system config |
| **Config Migrations** | Version-to-version config evolution |
| **GrowthBook Feature Flags** | 15+ feature gates for gradual rollout |
| **Organization Allowlists** | Org-level MCP server/plugin allowlists |
| **OpenTelemetry** | Full OTel instrumentation with BigQuery export |

### Priority: LOW (enterprise features)

---

## Recommended Sprint 62 Features (Top 20)

### Wave 1: Security (Critical)
1. **SSRF Guard** — Block private IPs/cloud metadata in web_fetch
2. **Unicode Sanitization** — NFKC normalize + strip Cf/Co/Cn from MCP/tool output
3. **Secret Scanner** — Detect API keys in tool output (30+ patterns from CC)
4. **Dangerous Path Detection** — Block rm -rf on system directories
5. **Additional Bash Patterns** — Add 7+ missing CC patterns (JQ RCE, newline injection, proc environ, etc.)

### Wave 2: Prompt Engineering (Cost Savings)
6. **Two-Tier Prompt Sections** — Cached vs uncached with dynamic boundary
7. **Sticky State Latching** — Prevent cache thrashing from mode toggles
8. **Attachment Deltas** — Only send changed MCP/skill/memory per turn
9. **Cache Break Detection** — Hash + log cache-breaking changes
10. **@include in Instructions** — Allow `@path` in AGENTS.md/CLAUDE.md

### Wave 3: Multi-Agent (HQ Enhancement)
11. **CacheSafeParams Sharing** — Forked agents share parent's prompt cache
12. **Sidechain Transcripts** — Per-agent JSONL audit trail
13. **Dream Agent** — Background memory consolidation
14. **File-Based Mailbox** — Cross-process agent communication

### Wave 4: Tools & Extensibility
15. **searchHint for ToolSearch** — 3-10 word capability phrases for deferred tool matching
16. **Content Replacement Budget** — Aggregate truncation across conversation
17. **Image in FileRead** — Resize/downsample images, return base64
18. **PDF Page Ranges** — Extract specific pages with token limits
19. **HTTP Hooks** — POST to external endpoints on tool events
20. **Bundled Skills** — Ship 5-10 built-in skills (debug, verify, simplify, loop, stuck)

---

## Metrics

| Metric | Value |
|--------|-------|
| Agents deployed | 9 |
| Areas scraped | MCP, Prompts, Agent SDK, Security, Tools, UI/UX, Streaming, New Features, Hooks |
| Features found | 60+ new (beyond Sprint 61's 18) |
| CC tools | 39 (vs AVA's 16) |
| CC bash patterns | 24 (vs AVA's ~22) |
| CC secret patterns | 30+ (vs AVA's 0) |
| CC hook events | 10+ (vs AVA's ~3) |
| CC skills | 16 bundled (vs AVA's 0) |
