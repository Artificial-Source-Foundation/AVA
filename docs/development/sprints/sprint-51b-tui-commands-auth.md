# Sprint 51b — TUI Commands, Model Browser, Provider Auth

> Slash command palette, dynamic model selector, `/connect` provider authentication, `/status`, `/diff`

**Parallel with Sprint 51a** (visual rework). Zero file overlap — this sprint owns command/modal/feature files, 51a owns visual/rendering files.

## Files this sprint OWNS (do NOT modify files owned by 51a)

**Modify:**
- `crates/ava-tui/src/app/commands.rs` — slash command handling
- `crates/ava-tui/src/app/mod.rs` — key handling (for `/` palette trigger)
- `crates/ava-tui/src/app/modals.rs` — modal dispatch
- `crates/ava-tui/src/widgets/command_palette.rs` — command palette modal
- `crates/ava-tui/src/widgets/model_selector.rs` — model selector modal
- `crates/ava-tui/src/state/input.rs` — autocomplete items
- `crates/ava-tui/src/state/keybinds.rs` — new keybindings (Ctrl+K)
- `crates/ava-tui/src/state/agent.rs` — recent models tracking

**Do NOT modify (owned by 51a):**
- `crates/ava-tui/src/state/theme.rs`
- `crates/ava-tui/src/ui/mod.rs`
- `crates/ava-tui/src/ui/status_bar.rs`
- `crates/ava-tui/src/ui/sidebar.rs`
- `crates/ava-tui/src/widgets/message_list.rs`
- `crates/ava-tui/src/widgets/composer.rs`
- `crates/ava-tui/src/widgets/welcome.rs`
- `crates/ava-tui/src/widgets/tool_approval.rs`
- `crates/ava-tui/src/state/messages.rs`
- `crates/ava-tui/src/rendering/markdown.rs`
- `crates/ava-tui/src/rendering/diff.rs`

**May create new files in:**
- `crates/ava-tui/src/widgets/` (e.g., `provider_connect.rs`)
- `crates/ava-tui/src/state/`

**May also modify:**
- `crates/ava-config/src/credential_commands.rs` — if needed for TUI integration
- `crates/ava-config/src/credentials.rs` — if needed

## Phase 1: Research (mandatory — do this BEFORE any code)

### Step 1 — Study OpenCode Commands & Auth

Read the OpenCode source in `docs/reference-code/opencode/`. Focus on:

1. **Command palette** — find `dialog-command.tsx`:
   - Command registration pattern (reactive `register(cb)` callbacks)
   - Categories, suggested commands, keybind display
   - Fuzzy search implementation
   - How selecting a command executes it

2. **Model browser** — find `dialog-model.tsx`:
   - Sections: Recent → Favorites → All (grouped by provider)
   - Cost badges ("Free" for zero-cost, price for paid)
   - Grayed-out unconfigured providers
   - Fuzzy search through model names
   - Current model marker
   - Favorite toggle keybind

3. **`/connect` auth flow** — find `auth.ts`:
   - Provider selection UI (priority ordering, recommended hints)
   - API key input with validation
   - OAuth auto-flow (open browser, poll callback)
   - Device code flow (paste code)
   - Credential storage format
   - Success/failure display

4. **Slash commands** — find `prompt/autocomplete.tsx`:
   - How `/` triggers autocomplete in the composer
   - What commands exist
   - Autocomplete filtering and selection

5. **Status display** — what `/status` shows

### Step 2 — Study Codex CLI Commands

Read Codex CLI source in `docs/reference-code/codex-cli/codex-rs/tui/src/`:

1. **Slash commands** — `slash_command.rs`:
   - 34 commands as enum variants
   - `available_during_task()` — commands blocked during agent execution
   - `supports_inline_args()` — commands that take arguments
   - How they're dispatched in `chatwidget.rs`

2. **Model selection** — `chatwidget.rs` (search for `open_model_popup`):
   - 3-tier flow: quick auto → all models → reasoning effort
   - Model metadata (description, is_default, show_in_picker)
   - Current model marker display

3. **Auth system** — `codex-rs/login/src/`:
   - OAuth callback server (`server.rs`)
   - Device code flow (`device_code_auth.rs`)
   - Token storage and refresh

4. **Provider config** — `codex-rs/core/src/model_provider_info.rs`:
   - `ModelProviderInfo` struct (name, base_url, env_key, etc.)
   - Built-in providers (OpenAI, Ollama, LM Studio)
   - User-defined providers via TOML config

### Step 3 — Study AVA Desktop App Auth

Read to understand what to reuse:

1. `src/components/settings/tabs/providers/` — provider cards, API key input UI patterns
2. `src/services/auth/` — OAuth flows (PKCE, device code)
3. `crates/ava-config/src/credential_commands.rs` — `CredentialCommand::Set/Remove/List/Test` (REUSE this)
4. `crates/ava-config/src/credentials.rs` — `CredentialStore`, env var fallback, placeholder detection, key redaction

### Step 4 — Audit Current AVA TUI Commands

Read to understand current state:
- `crates/ava-tui/src/app/commands.rs` — current slash commands
- `crates/ava-tui/src/widgets/command_palette.rs` — current palette items
- `crates/ava-tui/src/widgets/model_selector.rs` — current `default_models()` (5 hardcoded)
- `crates/ava-tui/src/state/input.rs` — current `refresh_autocomplete()` (5 items)
- `crates/ava-tui/src/state/keybinds.rs` — current keybindings

## Phase 2: Implementation

### Story 1 — `/` Opens Command Palette

When `/` is typed as the first character on an empty input, open the full command palette.

1. In `app/mod.rs` `handle_key()`, when `KeyCode::Char('/')`:
   - If `state.input.buffer.is_empty()` → open `ModalType::CommandPalette`, do NOT insert `/`
   - If buffer is non-empty → insert `/` normally (allows typing paths)

2. Update command palette in `widgets/command_palette.rs` with ALL commands:
   ```
   /help           Show available commands
   /model          Switch model
   /connect        Add provider credentials
   /providers      Show provider status
   /tools          List all tools
   /tools reload   Reload tools from disk
   /mcp            List MCP servers
   /mcp reload     Reload MCP config
   /session        Manage session
   /clear          Clear chat
   /compact        Compact context
   /diff           Show git changes
   /status         Show session info
   ```

3. Palette → selected command → execute via `handle_slash_command()`.

4. Add `Ctrl+K` keybinding in `state/keybinds.rs` as alias for command palette (matches OpenCode). Keep `Ctrl+/` working.

5. Update `refresh_autocomplete()` in `state/input.rs` to include all commands (so `/m` filters to `/model`, `/mcp`).

### Story 2 — Dynamic Model Browser

Replace `default_models()` in `widgets/model_selector.rs`.

1. **Build model list from multiple sources:**

   a. **Recent models** — add `recent_models: Vec<String>` to `AgentState` (track last 5 used). Show at top.

   b. **Configured providers** — load `CredentialStore` from `~/.ava/credentials.json`. For each provider with a valid API key, show curated models:
      - `anthropic` → claude-opus-4.6, claude-sonnet-4.6, claude-haiku-4.5
      - `openai` → gpt-5.4, gpt-5.3-codex, gpt-4o, gpt-4o-mini
      - `openrouter` → all above + google/gemini-3-flash-preview, moonshotai/kimi-k2.5
      - `gemini` → gemini-3-flash, gemini-3-pro, gemini-2.5-flash
      - `ollama` → async query `GET http://localhost:11434/api/tags`, parse JSON for installed models

   c. **Config file** — if `~/.ava/config.yaml` has a model set, include at top

2. **Group by provider with section headers:**
   ```
   ── Recent ──────────────────────────
   anthropic/claude-haiku-4.5

   ── Anthropic ───────────────────────
   claude-opus-4.6             $15/$75
   claude-sonnet-4.6           $3/$15
   claude-haiku-4.5            $1/$5

   ── OpenAI ──────────────────────────
   gpt-5.4                     $2.50/$15
   gpt-5.3-codex               $1.75/$7

   ── OpenRouter ──────────────────────
   (aggregated models from above)

   ── Ollama (local) ──────────────────
   llama3.3                    free
   codestral                   free

   ── Not Configured ──────────────────
   gemini                      /connect to add
   ```

3. **Show cost** from `estimate_cost_per_million()` in `common.rs`.

4. **Gray out unconfigured** providers with `(not configured — /connect to add)` hint.

5. Existing nucleo fuzzy search — just feed it the full list.

### Story 3 — `/connect` Provider Authentication

Interactive TUI credential management. Reuse `CredentialCommand` from `ava-config`.

1. Add `/connect` and `/providers` and `/disconnect` to `handle_slash_command()` in `app/commands.rs`.

2. Create new widget `widgets/provider_connect.rs` with `ModalType::ProviderConnect`.

3. **Screen 1 — Provider List** (when `/connect` or `/providers` with no args):
   ```
   ── Provider Status ──────────────────────────

     ✓  openrouter        sk-or...a1b2
     ✓  anthropic          sk-an...c3d4
     ✗  openai             not configured
     ✗  gemini             not configured
     ●  ollama             localhost:11434

   [Enter] Configure  [d] Disconnect  [t] Test  [Esc] Close
   ```
   - ✓ (success color) for configured, show redacted key (first 4 + last 4 chars)
   - ✗ (error color) for unconfigured
   - ● (primary color) for local providers (no key needed)
   - Arrow keys to navigate, Enter to configure selected

4. **Screen 2 — API Key Input** (after selecting a provider, or `/connect openrouter`):
   ```
   ── Configure OpenRouter ─────────────────────

   API Key: ●●●●●●●●●●●●●●●●●●●●

   Or set OPENROUTER_API_KEY in your environment.

   Base URL (optional): https://openrouter.ai/api/v1

   [Enter] Save  [Tab] Test Connection  [Esc] Cancel
   ```
   - Password-masked input (show `●` characters)
   - Provider-specific env var hint
   - Optional base URL field
   - Tab to test before saving

5. **Save** → `CredentialCommand::Set { provider, api_key, base_url }` (from `credential_commands.rs`)
   - Saved to `~/.ava/credentials.json` with 0o600 permissions
   - Show success TTL message in chat
   - Refresh model selector available models

6. **Test** → `CredentialCommand::Test { provider }` (already implemented)
   - Show `✓ Connected to OpenRouter` or `✗ Invalid API key (401)`

7. **`/disconnect <provider>`** → `CredentialCommand::Remove { provider }` + confirmation message

8. Register `ProviderConnect` modal in `app/modals.rs` with key handling.

### Story 4 — `/status` Command

Add `/status` to `handle_slash_command()`.

Display as a formatted message in chat:
```
Model: anthropic/claude-haiku-4.5 via openrouter
Tokens: 12,340 in / 3,456 out ($0.02)
Session: abc12345 (12 turns)
Tools: 19 built-in + 3 MCP + 2 custom
Context: 45% used (89K / 200K)
Working directory: /home/user/project
```

Pull data from `AgentState` fields (provider, model, token counts, session info, tools count).

### Story 5 — `/diff` Command

Add `/diff` to `handle_slash_command()`.

1. Run `git diff --stat` via `std::process::Command`
2. Also run `git status --short` for untracked files
3. Display combined output as a system message in chat:
   ```
    src/main.rs     | 12 +++---
    src/auth.rs     | 45 +++++++++++++------
    2 files changed, 35 insertions(+), 22 deletions(-)
    3 untracked files
   ```

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Manual TUI testing:
# 1. cargo run --bin ava
# 2. Type "/" on empty input → full command palette opens with all commands
# 3. Press Ctrl+K → same palette opens
# 4. Press Ctrl+M → model selector shows grouped models by provider, with costs
# 5. Type "/connect" → provider list with ✓/✗ status
# 6. Type "/connect openrouter" → API key input (masked)
# 7. Tab in key input → tests connection
# 8. Enter → saves credentials
# 9. Type "/providers" → same as /connect
# 10. Type "/disconnect openrouter" → removes credentials
# 11. Type "/status" → session info displayed
# 12. Type "/diff" → git changes displayed
# 13. All existing commands (/help, /model, /tools, /mcp, /clear, /compact) still work
```

## Rules

- Phase 1 (research) MUST complete before Phase 2
- Read the ACTUAL source code in `docs/reference-code/` — don't guess
- Do NOT modify files owned by Sprint 51a (listed above)
- Reuse `CredentialCommand` from `ava-config` — do NOT duplicate credential logic
- Mask API keys in all displays (first 4 + last 4 chars only)
- Keep model list curated per provider — don't fetch full API catalogs
- Use existing `nucleo` fuzzy search for palette and model selector
- All new slash commands must appear in `/help` output
- All widgets should use `theme` colors (do not hardcode new `Color::` values — if you need a color that doesn't exist in the theme, note it and 51a will add it)
- Conventional commit: `feat(tui): slash palette, model browser, provider auth`
