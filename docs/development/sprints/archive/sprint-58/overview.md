# Sprint 58: Modal System Revamp

## Goal

Complete redesign of the AVA TUI modal system. Fix scrolling bugs, upgrade visual design, and implement a reusable modal framework inspired by OpenCode's dialog system.

## Problems

1. **Scroll bug**: Provider connect list doesn't scroll past first page — `ensure_visible()` uses item index instead of rendered line position (doesn't account for section headers/blanks)
2. **No shared modal framework**: Each modal reimplements scroll, search, keybinds, and rendering independently
3. **Plain design**: No selection background highlight, no visual polish, inconsistent key hints
4. **No PageUp/PageDown/Home/End**: Only arrow keys work
5. **No mouse support**: Can't click to select items

## Approach

Build a reusable `SelectList` widget that all list-based modals share. Then rebuild each modal on top of it.

## Research References

- OpenCode `dialog-select.tsx` — scrollable list with smart center/edge scrolling, category headers, fuzzysort search, keyboard+mouse, keybind footer
- OpenCode `dialog.tsx` — stack-based modal manager with backdrop, ESC handling, focus restoration
- OpenCode `dialog-model.tsx` — model selector with favorites, recent, provider categories
- OpenCode `dialog-provider.tsx` — chained auth flow dialogs

## Prompts

1. `01-modal-revamp.md` — Full implementation

## Status: Complete
