# AVA Version Docs

These files track AVA release-position documentation. The shipped backend MVP runtime now reports `1.0.0`; future release-position docs still require a dedicated release-bump slice before their documented line becomes the compiled runtime version.

Current position:

- `1.1.md`: planning notes for the next release line. The compiled runtime still reports `1.0.0`; 1.1 needs a dedicated implementation and release-bump slice before it becomes shipped evidence.
- `1.0.md`: current shipped backend MVP. The compiled runtime reports `1.0.0` after the 0.90 release-candidate evidence map, OpenAI and Kimi-for-coding live smokes, focused/full CTest, and release-bump validation passed.
- `0.90.md`: v1 release-candidate completion ledger. It owns the 1.0 capability disposition, test evidence map, and provider live-smoke status that justified the 1.0 runtime bump.
- `0.80.md`: extension API stabilization target. The local plugin authoring guide, sample plugin, real-sample headless RPC smoke coverage, compatibility policy, minimal golden fixtures, the then-current MCP resource deferral decision, focused audit/failure contract tests, and OpenAI 5.5 manual headless release-validation pass are implemented; read-style MCP resources later landed behind `mcp.resource.read`.
- `0.75.md`: source-backed extension foundation line. Unified command discovery/invocation, plugin diagnostics, plugin prompt/skill resources and commands, and MCP stdio tool/prompt foundations are implemented and validated by later release-candidate evidence.

Post-1.0 path:

- Use `docs/product/mvp-baseline.md` for the current Pi-first/OpenCode-second product MVP baseline and gap ledger.
- Use `docs/product/capabilities-1.1.md` for the current backend-only 1.1 candidate capability list.
- Keep Anthropic, Moonshot, and OpenRouter-compatible live smokes as follow-up provider-breadth validation unless the product decision changes.
- Use `docs/roadmap/backend.md` and the post-1.0 roadmap sections for 1.1+ follow-up planning.

Historical or supporting ledgers:

- `0.70.md`: reasoning and model lifecycle closeout bundled into the 0.65 pass for protocol docs, focused tests, and export polish.
- `0.65.md`: provider-native hardening. Anthropic and compatible-provider paths have contract-backed offline/fake coverage; live credentialed smokes remain deferred release evidence when unavailable.
- `0.33.md`: detailed backend maturity slice ledger. This file contains the evidence for the jump to the 0.60 line and should remain append-only for slice-level validation details.
- `0.32.md`: ncursesw TUI replacement baseline. The compiled project/runtime version has since advanced to `1.0.0`; this file remains historical evidence for the terminal baseline.
- `0.60.md`: backend platform catch-up line that reconciled roadmap Phase 0-6 progress after the oversized 0.33 ledger.
- `0.21.md`: TUI revival slice.
- `0.3.md`: historical workflow-depth milestone plan.
- `0.2.md`: hardened backend/tooling milestone.
- `0.1.md`: initial focused assistant milestone.
