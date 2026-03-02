# QA Checklist — Sprint 23 (Launch Readiness)

> Manual test matrix for verifying the desktop app before release.

## Core Functionality

- [ ] `npm run tauri dev` — app starts without console errors
- [ ] New session — type message — streaming response works
- [ ] Agent mode — tool execution — completion
- [ ] Multi-turn conversation — context maintained across turns
- [ ] Session persistence — close and reopen app — messages preserved

## Sessions

- [ ] Create new session (Ctrl+N)
- [ ] Rename session (right-click → Rename)
- [ ] Duplicate session (right-click → Duplicate)
- [ ] Fork session (right-click → Fork from here)
- [ ] Archive session (right-click → Archive) — disappears from main list
- [ ] Archived section — expand — see archived sessions
- [ ] Unarchive session — returns to main list
- [ ] Delete session — confirmation dialog — permanent removal
- [ ] Session slug — appears below session name in sidebar
- [ ] Busy indicator — spinner icon when agent is executing
- [ ] Branch at message — creates new session with messages up to that point

## Settings

- [ ] Change accent color — applies immediately
- [ ] Change density (compact/default/comfortable) — layout adjusts
- [ ] Switch provider — new messages use selected provider
- [ ] Model selector — dropdown shows available models
- [ ] Settings persist across app restarts

## File Operations

- [ ] File explorer — navigate project tree
- [ ] Open file in editor (double-click)
- [ ] File operations tracked in session panel

## Terminal

- [ ] Terminal view — `ls` command — output appears
- [ ] Terminal command history tracked

## Plugins

- [ ] Plugin marketplace — browse available plugins
- [ ] Install plugin from catalog
- [ ] Enable/disable installed plugin
- [ ] Plugin install from git URL
- [ ] Plugin version checking (update available indicator)

## Visual / Theme

- [ ] Light mode — all components readable, no invisible text
- [ ] Dark mode — default, no contrast issues
- [ ] Glass effects — panels have correct blur/transparency

## Keyboard Shortcuts

- [ ] Ctrl+B — Toggle sidebar
- [ ] Ctrl+, — Open settings
- [ ] Ctrl+N — New session
- [ ] Ctrl+J — Toggle terminal
- [ ] Ctrl+K — Command palette
- [ ] Escape — Cancel current action / close modal

## Responsive / Layout

- [ ] Sidebar collapse/expand
- [ ] Right panel resize
- [ ] Minimum window size — no layout breaks

## Platform Testing

- [ ] Linux GNOME — full functionality
- [ ] Linux KDE — full functionality
- [ ] Linux Cosmic — full functionality (if available)

## Agent System

- [ ] Praxis mode — delegation to leads/workers
- [ ] Explorer agent — read-only exploration works
- [ ] Tool approval dialog — appears for risky tools
- [ ] Sandbox mode — file writes captured for review

## Context & Tokens

- [ ] Context window usage indicator updates during conversation
- [ ] Token count shown per message
- [ ] Auto-compaction triggers when context fills up
