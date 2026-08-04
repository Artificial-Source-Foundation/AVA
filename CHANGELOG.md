# Changelog

All notable changes to AVA are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and version identifiers here do not imply that a tag, artifact, package, or external release has been published.

## [Unreleased]

### Added

- Community documentation and GitHub contribution templates.
- Current architecture, codebase, build-configuration, environment-variable, LSP, built-in-tool, subagent/background-job, troubleshooting, and documentation-policy references.
- Offline source Markdown relative-target validation with focused CTest coverage.
- Added the additive `ava.plugin.v1` host-rendered plugin UI contract: exact `ui.status`, `ui.widget`, `ui.select`, and `ui.confirm` manifest capabilities and bounded JSONL records/actions; high-risk `plugin.ui.present` preflight before child launch; exact direct foreground interactive-TUI authority only; complete nontruncated plugin/command attribution with fail-closed display-cell fitting; host confirmation defaulting to Cancel; a 120-second absolute deadline; finite external-disable revocation; and deterministic child/presenter cleanup. UI authority remains unavailable to RPC, ACP, print, line/headless/non-TTY, model/tool, hook, background, queued, synthetic, and plugin-to-plugin paths. UI text is strict UTF-8 with control/C1/bidi rejection and is ephemeral—excluded from sessions, responses/events, exports, provider context, and diagnostics. Arbitrary markup/native renderers, browser/auth/file/secret/form access, and marketplace delivery remain deferred; coverage is deterministic plus credential-free tmux rather than an untested real-terminal claim.

### Changed

- Reorganized first-party documentation into explicit core, interfaces, extensions, operations, development, security, product, plans, roadmap, and history categories while preserving fixed normative contract and evidence paths.
- Added one task-ordered human documentation spine, concise category indexes, nested documentation-maintenance instructions, and a root `llms.txt` map for automated readers.
- Refreshed build, testing, Docker, release, support, and Linux host-artifact documentation; packaged end-user references now include context resources, providers, security, subagents, terminal setup, themes/keybindings, thinking modes, environment, LSP, tools, and troubleshooting guides.

## Historical release-position documentation

Historical capability and release-position detail is maintained in [`docs/versions/`](docs/versions/README.md). These ledgers document repository history and planning context; consult them rather than treating this changelog as evidence of a published release.
