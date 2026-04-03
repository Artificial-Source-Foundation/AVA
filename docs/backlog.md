# Backlog — 2026-04-02

This file is the active product/work backlog, not a full historical changelog.

Use it for:

- current work that should be tackled next
- follow-up cleanup that is still worth tracking
- a short list of recent completions for context

Use `CHANGELOG.md` for the full release history.

## Now

1. **Document and test cancel preservation semantics** — Current TUI/web/desktop cancel paths appear to preserve prior conversation state and keep partial in-flight output where available, but this still needs direct regression coverage so future cancel/refactor work does not reintroduce message-loss bugs.
2. **UI backlog truth pass** — The desktop redesign is largely shipped, but a few old backlog claims still overstate cleanup: the Inspector description does not match the current panel contents, legacy Team-mode surfaces still exist in code, and some renamed HQ surfaces still carry old path/component names.

## Next

1. **Team chat view cleanup** — HQ is the primary replacement, but legacy Team-mode components/stores still exist in code and should either be removed or documented as intentional.
2. **Inspector copy pass** — Backlog/docs copy should match the actual right-panel contents and naming.
3. **Keep HQ/docs wording in sync with the shipped shell** — HQ docs are much healthier now, but shared-chat/sidebar/settings/browser-route wording should stay aligned as the HQ shell evolves.
4. **Installation follow-through** — The docs now describe quick CLI install, desktop downloads, source install, and contributor setup, but the product surface still lacks friendlier package-manager paths like a Windows one-liner, `winget`, or documented Homebrew flow.
5. **Extensions onboarding examples** — The extensions guide now documents the real extension surface, but it still lacks a short end-to-end “build your first extension” tutorial for custom tools, commands, or power plugins.

## Recently Completed

1. **Extensions guide refresh** — `docs/plugins.md` now matches the actual shipped extension surface and covers MCP servers, custom tools, custom slash commands, skills/instructions, trust gating, and the real power-plugin hook surface.
2. **Full CLI help polish pass** — `ava --help` and the exposed subcommands now use clearer grouping, shorter flag descriptions, and example-driven help text.
3. **Installation docs pass** — README now exposes four clear install paths instead of only source builds: quick Linux/macOS CLI install via `install.sh`, desktop downloads from GitHub Releases, source install, and contributor setup. `docs/install.md` now gives copy-paste commands, platform notes, first-run setup, and current gaps.
4. **Docs navigation + reference cleanup** — The docs index now matches the live docs tree, stale doc-path references were removed from the main entry points and contributor instructions, `CODEBASE_STRUCTURE.md` was rewritten into a current lightweight repo map, and `docs/troubleshooting/README.md` now indexes the troubleshooting notes.
5. **HQ docs refresh** — `docs/hq/README.md` now describes the real HQ runtime instead of the old mock-data frontend, including shared chat reuse, shipped HQ shell/settings behavior, browser routes, and the current persistence model.

## Earlier Major Waves

These are no longer active backlog items, but they are useful context for the current state of the repo.

1. **Desktop UI revamp** — The macOS-style desktop redesign shipped across the sidebar, chat composer, message stream, dashboard, settings, dialogs, onboarding, Inspector, and HQ surfaces.
2. **HQ convergence and hardening** — HQ now uses more of the shared AVA chat/plan primitives, has real backend persistence, browser-mode HQ routes, better plan/runtime fidelity, and cleaner Director/shell behavior.
3. **Desktop reliability/performance passes** — Multiple rounds of state-lifecycle, reconnect, scroll/focus, listener/timer, streaming, token/theme, and responsiveness cleanup landed across the desktop app.
4. **Provider/runtime/tooling hardening** — Provider routing/streaming fixes, tool/runtime safety improvements, MCP/plugin fault handling, backend cancel/crash hardening, and better deterministic test/runtime behavior all landed during the v3 push.
5. **Stress-test and polish waves** — End-to-end desktop/web/headless sweeps, slash-command expansion, config sync, compaction work, and per-model prompt tuning all shipped as part of the v3 stabilization work.
