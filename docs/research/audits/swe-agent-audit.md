# SWE Agent Deep Audit

> Comprehensive analysis of SWE-agent's AI coding agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/swe-agent/`

---

## Overview

SWE-agent is a **research-grade AI coding assistant** built at Princeton and Stanford, designed for academic research and SWE-bench evaluation. Its core differentiator is a **windowed file editing system** where the agent never sees entire files at once — instead viewing through a configurable sliding window (default 100 lines). SWE-agent implements the **ACI (Agent-Computer Interface)** — a Docker-based bash session for persistent execution. It pioneered **history processors** — a chain-of-responsibility pipeline where each processor transforms the `History` before LLM queries. SWE-agent supports **11 output parsing formats** from simple action extraction to OpenAI function calling. It implements **action samplers** — generating N candidate responses and picking the best. The **reviewer agent loop** validates outputs with a separate LLM. SWE-agent uses **SWE-ReX** for remote containerized execution.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Windowed Edit** | 100-line viewing window | `tools/windowed/install.sh`, `config/sweagent_0_7/07.yaml` |
| **str_replace_editor** | 5 commands: view, create, str_replace, insert, undo_edit | `tools/edit_anthropic/bin/str_replace_editor` |
| **Line-Range Edit** | `edit <start_line>:<end_line>` syntax | `tools/windowed_edit_linting/bin/edit` |
| **Unique Match Enforcement** | Rejects if 0 or 2+ matches | `tools/windowed/lib/windowed_file.py` |
| **Flake8 Lint Gating** | Auto-revert on syntax errors | `tools/windowed/lib/flake8_utils.py` |
| **Self-Correction** | `correct_edit()` feeds errors to LLM | `tools/windowed/lib/flake8_utils.py` |
| **Three Edit Variants** | linting, rewrite, replace | `tools/windowed_edit_linting/`, `tools/windowed_edit_rewrite/`, `tools/windowed_edit_replace/` |
| **WindowExpander** | Smart viewport expansion | `tools/edit_anthropic/bin/str_replace_editor:228-339` |
| **Filemap** | Tree-sitter-based file summaries | `tools/edit_anthropic/bin/str_replace_editor:190-225` |
| **Window Navigation** | open, goto, scroll_up, scroll_down | `tools/windowed/bin/` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **History Processors Pipeline** | Chain-of-responsibility pattern | `sweagent/agent/history_processors.py` |
| **7 Processor Types** | Default, LastNObservations, ClosedWindow, TagToolCall, CacheControl, RemoveRegex, ImageParsing | `sweagent/agent/history_processors.py` |
| **LastNObservations** | Keeps only last N observations | `sweagent/agent/history_processors.py` |
| **ClosedWindowHistoryProcessor** | Summarizes stale file windows | `sweagent/agent/history_processors.py` |
| **Trajectories (JSON)** | Full `.traj` files for reproducibility | `sweagent/agent/agents.py` |
| **Trajectory Inspector** | Textual TUI with vim bindings | `sweagent/run/inspector_cli.py` |
| **Web-Based Inspector** | Flask-based trajectory viewer | `sweagent/inspector/server.py` |
| **CacheControl** | Anthropic prompt caching | `sweagent/agent/history_processors.py` |
| **Demonstration Replay** | Replay trajectories as demos | `sweagent/run/run_replay.py` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **ACI (Agent-Computer Interface)** | Docker-based bash session | `sweagent/environment/swe_env.py` |
| **Tool Bundles** | Bash scripts grouped and uploaded | `tools/` |
| **11 Output Parsers** | From simple to function calling | `sweagent/tools/parsing.py` |
| **Retry/Requery Loop** | Handles format errors, blocked actions | `sweagent/agent/agents.py` |
| **Retry Token Mechanism** | `###SWE-AGENT-RETRY-WITH-OUTPUT###` | Various tool scripts |
| **Action Samplers** | Generate N candidates, pick best | `sweagent/agent/action_sampler.py` |
| **Reviewer Agent Loop** | LLM validates outputs | `sweagent/agent/reviewer.py` |
| **4 Retry Layers** | API retry, format requery, autosubmission, reviewer | `sweagent/agent/agents.py` |
| **Autosubmission** | Salvages git diff on fatal errors | `sweagent/agent/agents.py` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **SWE-ReX** | Remote containerized execution | External dependency |
| **Docker Sandbox** | Default containerized execution | `sweagent/environment/swe_env.py` |
| **Multiple Runtime Types** | Docker, Remote, Kubernetes, Local | `sweagent/runtime/__init__.py` |
| **No Local Execution** | All execution in containers | Architecture |
| **Tool Bundles** | Sandboxed tool scripts | `tools/` |
| **Command Blocklist** | Prevents dangerous commands | `sweagent/tools/tools.py` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Minimal Design** | Single YAML config governs all | `config/default.yaml` |
| **Research-Grade Focus** | Academic evaluation emphasis | Architecture |
| **SWE-bench Integration** | Auto-submit to sb-cli | `sweagent/run/hooks/swe_bench_evaluate.py` |
| **Trajectory Logging** | Full `.traj` JSON files | `sweagent/agent/agents.py` |
| **Makefile** | 370 lines of build targets | `Makefile` |
| **Hook System** | Agent and run hooks | `sweagent/agent/hooks/abstract.py`, `sweagent/run/hooks/abstract.py` |
| **Batch Execution** | Parallel runs with progress | `sweagent/run/run_batch.py` |
| **Rich Progress Bars** | Exit status table, cost display | `sweagent/run/_progress.py` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **Windowed File Editing** | 100-line viewing window | `tools/windowed/`, `tools/edit_anthropic/` |
| **History Processors Pipeline** | Chain-of-responsibility transforms | `sweagent/agent/history_processors.py` |
| **11 Output Parsers** | Multiple parsing strategies | `sweagent/tools/parsing.py` |
| **Action Samplers** | Best-of-N selection | `sweagent/agent/action_sampler.py` |
| **Reviewer Agent Loop** | LLM validates outputs | `sweagent/agent/reviewer.py` |
| **SWE-ReX** | Remote containerized execution | External |
| **ACI** | Agent-Computer Interface | `sweagent/environment/swe_env.py` |
| **Tool Bundles** | Sandboxed bash scripts | `tools/` |
| **Trajectory Replay** | Deterministic re-execution | `sweagent/run/run_replay.py` |
| **4 Retry Layers** | Comprehensive reliability | `sweagent/agent/agents.py` |

---

## Worth Stealing (for AVA)

### High Priority

1. **History Processors Pipeline** (`sweagent/agent/history_processors.py`)
   - Chain-of-responsibility transforms
   - 7 processor types
   - Composable, testable

2. **Action Samplers** (`sweagent/agent/action_sampler.py`)
   - Generate N candidates, pick best
   - Improves quality at compute cost

3. **Windowed File Editing** (`tools/windowed/`, `tools/edit_anthropic/`)
   - 100-line viewing window
   - Reduces context usage

### Medium Priority

4. **11 Output Parsers** (`sweagent/tools/parsing.py`)
   - Multiple parsing strategies
   - Graceful degradation

5. **Reviewer Agent Loop** (`sweagent/agent/reviewer.py`)
   - LLM validates outputs
   - ScoreRetryLoop, ChooserRetryLoop

6. **Trajectory Logging** (`sweagent/agent/agents.py`)
   - Full `.traj` JSON files
   - Reproducibility, debugging

7. **4 Retry Layers** (`sweagent/agent/agents.py`)
   - API retry, format requery, autosubmission, reviewer
   - Comprehensive reliability

### Lower Priority

8. **SWE-bench Integration** — Only needed for evaluation
9. **Tool Bundles** — Bash scripts less flexible than TypeScript
10. **Window Navigation** — Desktop app has different needs

---

## AVA Already Has (or Matches)

| SWE-agent Feature | AVA Equivalent | Status |
|-------------------|----------------|--------|
| Docker sandbox | Docker sandbox extension | ✅ Parity |
| History processors | Middleware pipeline | ✅ Similar |
| Windowed editing | (Not implemented) | ❌ Gap |
| Action samplers | (Not implemented) | ❌ Gap |
| Reviewer loop | (Not implemented) | ❌ Gap |
| 11 output parsers | Tool calling | ✅ Different approach |
| Trajectory logging | Session logging | ✅ Parity |
| Tool bundles | Extensions | ✅ Better |
| SWE-bench | (Not implemented) | ❌ Gap |

---

## Anti-Patterns to Avoid

1. **Python-Only** — Limits extension ecosystem; TypeScript is more accessible
2. **Research-Only Focus** — Less end-user polish; AVA targets developers
3. **Complex YAML Config** — Single file governs all; prefer modular config
4. **Windowed Editing Complexity** — Adds cognitive overhead
5. **No Desktop UI** — Terminal-only limits adoption

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **Enhanced History Processors** — Better processor chain
- **Improved Action Samplers** — Better candidate selection
- **Reviewer Loop Refinements** — Better validation
- **SWE-bench v3 Support** — Latest benchmark integration

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `sweagent/agent/history_processors.py` | 399 | History processors pipeline |
| `sweagent/agent/agents.py` | ~1,200 | Agent loop, retry layers |
| `sweagent/agent/action_sampler.py` | ~200 | Action sampling |
| `sweagent/agent/reviewer.py` | ~300 | Reviewer agent loop |
| `sweagent/tools/parsing.py` | ~300 | 11 output parsers |
| `tools/windowed/lib/windowed_file.py` | 315 | Windowed file management |
| `tools/edit_anthropic/bin/str_replace_editor` | 710 | Anthropic-style editor |
| `sweagent/environment/swe_env.py` | 276 | ACI Docker wrapper |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
