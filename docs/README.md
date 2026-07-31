# AVA Documentation

Use this task- and audience-oriented index for current AVA documentation. Historical release-position ledgers and plans are clearly separated from current user and maintainer references.

## Use and operate AVA

| Task | Start here | Related current references |
| --- | --- | --- |
| Install, run, and choose an interface | [USAGE.md](USAGE.md) | [features.md](features.md), [headless-protocol.md](headless-protocol.md), [Docker](docker/README.md) |
| Configure auth, models, resources, and permissions | [CONFIG.md](CONFIG.md) | [environment-variables.md](environment-variables.md), [providers.md](providers.md), [context-resources.md](context-resources.md) |
| Understand model-visible operations | [tools.md](tools.md) | [security-sandboxing.md](security-sandboxing.md), [LSP](lsp.md), [MCP](mcp.md), [plugins](plugin-system.md) |
| Fix a failure | [troubleshooting.md](troubleshooting.md) | [diagnostics.md](diagnostics.md), [terminal setup](terminal-setup.md), [SUPPORT.md](../SUPPORT.md) |
| Customize the terminal UI | [themes-keybindings.md](themes-keybindings.md) | [terminal-setup.md](terminal-setup.md), [thinking-modes.md](thinking-modes.md) |
| Work with sessions | [session-format.md](session-format.md) | [USAGE.md](USAGE.md), [session versioning](engineering/session-versioning.md) |
| Run in a container | [docker/README.md](docker/README.md) | [security-sandboxing.md](security-sandboxing.md) |

The optional Qt Quick interface is an experimental prototype; see [desktop-qml.md](desktop-qml.md).

## Automate or extend AVA

Choose the contract that matches the client:

- [rpc-protocol.md](rpc-protocol.md): normative proprietary AVA RPC v1 for automation and custom clients. It is not generic JSON-RPC, ACP, or Pi RPC.
- [acp.md](acp.md): stable ACP v1 JSON-RPC 2.0 editor endpoint and client setup; [acp-support.json](acp-support.json) is the machine-checked support profile, and [interop/evidence/README.md](interop/evidence/README.md) defines evidence labels.
- [headless-protocol.md](headless-protocol.md): behavior shared by print and RPC headless modes.
- [plugin-system.md](plugin-system.md): local plugin authoring; [plugin-compatibility-policy.md](plugin-compatibility-policy.md) defines compatibility and golden-fixture policy.
- [mcp.md](mcp.md): local stdio MCP configuration and safety boundaries.
- [lsp.md](lsp.md): optional local language-server configuration, model tools, permissions, bounds, and cleanup.
- [context-resources.md](context-resources.md): context files, prompts, skills, subagents, plugins, MCP, LSP, and project trust.

## Contribute and maintain

| Task | Current reference |
| --- | --- |
| Join the project | Root [CONTRIBUTING.md](../CONTRIBUTING.md), [Code of Conduct](../CODE_OF_CONDUCT.md), [Governance](../GOVERNANCE.md), [Security](../SECURITY.md), and [Support](../SUPPORT.md) |
| Build and configure a checkout | [CONTRIBUTING.md](CONTRIBUTING.md) and [build-configuration.md](build-configuration.md) |
| Navigate the implementation | [architecture.md](architecture.md), [engineering/codebase-guide.md](engineering/codebase-guide.md), and [AGENTS.md](../AGENTS.md) |
| Run and classify tests | [TESTING.md](TESTING.md) |
| Write and review documentation | [documentation.md](documentation.md) |
| Apply C++ safety rules | [engineering/cpp-safety-rules.md](engineering/cpp-safety-rules.md) |
| Add a side effect | [engineering/side-effect-safety-checklist.md](engineering/side-effect-safety-checklist.md) |
| Change persisted sessions | [engineering/session-versioning.md](engineering/session-versioning.md) and [session-format.md](session-format.md) |
| Prepare the Linux host artifact | [release-checklist.md](release-checklist.md) |

The architecture, codebase guide, build-configuration reference, and documentation policy describe the source checkout and are intentionally not installed in the end-user host artifact.

## Security and privacy

- [security-sandboxing.md](security-sandboxing.md): practical trust, permission, plugin/MCP, and external sandbox guidance.
- [security/containment.md](security/containment.md): normative verified command-containment scope and limitations.
- [diagnostics.md](diagnostics.md): passive doctor, private traces, typed last-failure state, sanitized support exports, and privacy exclusions.
- [engineering/side-effect-safety-checklist.md](engineering/side-effect-safety-checklist.md): maintainer review questions for new side effects.

## Product status, roadmap, and history

These pages are useful for planning and evidence, but they do not replace the current references above and do not prove publication by themselves:

- Current capability views: [product/mvp-baseline.md](product/mvp-baseline.md), [product/mvp-coverage-ledger.md](product/mvp-coverage-ledger.md), and [product/backend-capabilities-1.0.md](product/backend-capabilities-1.0.md).
- Forward plans: [product/capabilities-1.1.md](product/capabilities-1.1.md), [product/tooling-plan.md](product/tooling-plan.md), [product/package-manager-plan.md](product/package-manager-plan.md), and [product/parallel-tools-plan.md](product/parallel-tools-plan.md).
- Roadmaps: [roadmap/backend.md](roadmap/backend.md), [roadmap/frontend.md](roadmap/frontend.md), [roadmap/frontend-evidence-baseline.md](roadmap/frontend-evidence-baseline.md), [roadmap/backend-usability.md](roadmap/backend-usability.md), and [roadmap/backend-maturity-baseline.md](roadmap/backend-maturity-baseline.md).
- Goal packages: [goals/README.md](goals/README.md).
- Historical release-position ledgers: [versions/README.md](versions/README.md), including [versions/1.0.md](versions/1.0.md) and the current 1.1 planning journal in [versions/1.1.md](versions/1.1.md).

Local `reference-code/pi/`, `reference-code/opencode/`, and `reference-code/grok-build/` trees are comparative behavior references only. They are not AVA architecture, implementation, documentation authority, or build input.
