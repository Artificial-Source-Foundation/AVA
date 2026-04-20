---
title: "AVA Documentation"
description: "Public-facing docs entrypoint organized with a Diátaxis-style layout."
order: 1
updated: "2026-04-20"
---

# AVA Documentation

This is the public-facing documentation entrypoint for AVA.

Use this page when you want user-facing docs first. Contributor, maintainer, and architecture material is linked separately below.

## Choose by goal

| I want to... | Start here |
|---|---|
| Install AVA for terminal use | [How-to: Install AVA](how-to/install.md) |
| Download or build the desktop app | [How-to: Download AVA Desktop](how-to/download-desktop.md) |
| Get running quickly | [Tutorial: First run](tutorials/first-run.md) |
| Learn the first practical workflow | [Tutorial: Your first workflow](tutorials/your-first-workflow.md) |
| Configure providers and auth | [How-to: Configure providers and local settings](how-to/configure.md) |
| Run AVA in CI or scripts | [How-to: Run AVA in CI/headless automation](how-to/ci-headless-automation.md) |
| Troubleshoot common setup/runtime failures | [Troubleshooting: Common errors](troubleshooting/common-errors.md) |

## Start here (public)

1. [Tutorial: First run](tutorials/first-run.md)
2. [Tutorial: Your first workflow](tutorials/your-first-workflow.md)
3. [How-to: Install AVA](how-to/install.md)
4. [How-to: Download AVA Desktop](how-to/download-desktop.md)
5. [How-to: Configure providers and local settings](how-to/configure.md)
6. [How-to: Use local models with Ollama](how-to/ollama-local-models.md)
7. [How-to: Run AVA Locally](how-to/run-locally.md)
8. [How-to: Run tests and checks](how-to/test.md)
9. [How-to: Run AVA in CI/headless automation](how-to/ci-headless-automation.md)
10. [How-to: Run your first workflow](how-to/first-workflow.md)
11. [Explanation: AVA surfaces and docs boundaries](explanation/ava-surfaces-and-doc-boundaries.md)
12. [Explanation: Security and trust](explanation/security-and-trust.md)
13. [Reference docs](reference/README.md)
14. [Reference: Install and release paths](reference/install-and-release-paths.md)
15. [Troubleshooting: Common errors](troubleshooting/common-errors.md)
16. [Troubleshooting: Ollama local models](troubleshooting/ollama-local-models.md)

The tutorial and how-to pages are the main public layer. Reference pages are the factual source of truth for commands, providers, install artifacts, filesystem layout, and other stable details. Troubleshooting pages stay focused on recovery steps.

## Internal and maintainer material (kept separate)

These pages remain important, but they are not the public quick-start path:

1. [Project roadmap and backlog](project/README.md)
2. [Architecture docs](architecture/README.md)
3. [Operations docs](operations/README.md)
4. [Contributor workflow](contributing/README.md)

## Grounding

Claims in the new public pages are grounded in these repository sources:

1. [`README.md`](../README.md) for install, auth, and run commands
2. [`docs/reference/commands.md`](reference/commands.md) for CLI/subcommand workflow
3. [`docs/reference/providers-and-auth.md`](reference/providers-and-auth.md) for provider/auth behavior
4. [`Justfile`](../Justfile) and [`docs/contributing/development-workflow.md`](contributing/development-workflow.md) for first contributor workflow checks
5. [`install.sh`](../install.sh), [`dist-workspace.toml`](../dist-workspace.toml), and [`.github/workflows/release.yml`](../.github/workflows/release.yml) for binary install/release path claims
