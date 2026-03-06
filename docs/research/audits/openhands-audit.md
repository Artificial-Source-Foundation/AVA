# OpenHands Deep Audit

> Comprehensive analysis of All-Hands AI OpenHands agent implementation
> Audited: 2026-03-05
> Based on codebase at `docs/reference-code/openhands/`

---

## Overview

OpenHands is a research-grade AI coding agent platform distinguished by its **event-sourced architecture** where every action and observation is a typed `Event` persisted to a `FileStore` and dispatched through a pub/sub `EventStream`. Its core differentiator is **9 condenser strategies** for context management — the most sophisticated of any competitor — including Recent, LLM-summarize, Amortized, Observation-masking, Structured, Hybrid, Browser-turn, Identity, and No-op. OpenHands implements **Docker sandbox by default** (the most secure execution model) with multiple runtime types (EventStream, E2B, Modal, Kubernetes, Remote, Local). The **AgentController** orchestrates the loop: subscribes to stream, feeds history into `Agent.step()`, emits actions back to stream where `Runtime` executes them. Multi-agent delegation is handled via `AgentDelegateAction`/`AgentDelegateObservation` with shared event stream but separate `State` slices.

---

## Key Capabilities

### Edit System

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Dual Edit Strategy** | ACI-based `str_replace_editor` (default) + LLM-based draft editor | `openhands/agenthub/codeact_agent/tools/str_replace_editor.py`, `openhands/agenthub/codeact_agent/tools/llm_based_edit.py` |
| **str_replace_editor** | 5 commands: view, create, str_replace, insert, undo_edit | `openhands/agenthub/codeact_agent/tools/str_replace_editor.py` |
| **Windowed Edit** | `view_range` for viewing, `start`/`end` for LLM-based | `openhands/agenthub/codeact_agent/tools/str_replace_editor.py`, `openhands/runtime/utils/edit.py` |
| **Unique Match Enforcement** | Exact unique string match for `old_str` | `openhands/agenthub/codeact_agent/tools/str_replace_editor.py` |
| **Lint Gating** | flake8-based, auto-revert on errors | `openhands/linter/__init__.py` |
| **Self-Correction** | `correct_edit()` feeds lint errors back to LLM | `openhands/runtime/utils/edit.py` |
| **Chunk Localizer** | Suggests relevant code regions via LCS | `openhands/utils/chunk_localizer.py` |

### Context & Memory

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **9 Condenser Strategies** | Recent, LLM-summarize, Amortized, Observation-masking, Structured, Hybrid, Browser-turn, Identity, No-op | `openhands/memory/condenser/impl/` |
| **Condenser Framework** | Abstract base + registry pattern | `openhands/memory/condenser/condenser.py` |
| **View Abstraction** | Reconstructs visible history via `CondensationAction` replay | `openhands/memory/view.py` |
| **Event-Sourced EventStream** | Pub/sub with subscriber threading | `openhands/events/stream.py` |
| **LLMSummarizing** | Structured summary with 9 sections | `openhands/memory/condenser/impl/llm_summarizing_condenser.py` |
| **AmortizedForgetting** | Drops middle half without summarization | `openhands/memory/condenser/impl/amortized_forgetting_condenser.py` |
| **ObservationMasking** | Replaces content outside attention window | `openhands/memory/condenser/impl/observation_masking_condenser.py` |
| **Token Management** | Per-event truncation, `max_message_chars`, prompt caching | `openhands/events/serialization/event.py`, `openhands/memory/conversation_memory.py` |
| **Micro-Agent Knowledge** | Trigger-based keyword matching | `openhands/memory/memory.py` |

### Agent Loop & Reliability

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Event-Sourced Architecture** | EventStream → Agent.step() → Actions → Runtime → Observations | `openhands/events/stream.py`, `openhands/controller/agent_controller.py` |
| **AgentController** | Central orchestrator with 5 loop scenarios | `openhands/controller/agent_controller.py` |
| **Docker Runtime** | Default containerized execution | `openhands/runtime/impl/docker/docker_runtime.py` |
| **Multiple Runtime Types** | Docker, Remote, Kubernetes, Local, E2B, Modal | `openhands/runtime/` |
| **StuckDetector** | 5 loop scenarios: repeated pairs, repeated errors, monologues, alternating, context window | `openhands/controller/stuck.py` |
| **LoopRecoveryAction** | 3 recovery strategies | `openhands/events/action/agent.py` |
| **Multi-Agent Delegation** | `AgentDelegateAction`/`AgentDelegateObservation` | `openhands/events/action/agent.py`, `openhands/events/observation/delegate.py` |
| **Error Recovery** | Layered: API retry, format requery, autosubmission, reviewer loop | `openhands/agent/agents.py` |

### Safety & Permissions

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Docker Sandbox (Default)** | Most secure execution model | `openhands/runtime/impl/docker/docker_runtime.py` |
| **Multiple Runtime Types** | Docker, Remote (gVisor/Sysbox), Kubernetes, Local, E2B, Modal | `openhands/runtime/__init__.py` |
| **Confirmation Mode** | `SecurityConfig.confirmation_mode` | `openhands/core/config/security_config.py` |
| **Action Security Risk** | LOW / MEDIUM / HIGH / UNKNOWN | `openhands/events/action/action.py` |
| **Security Analyzer Subsystem** | Invariant, LLM, GraySwan backends | `openhands/security/` |
| **OSV Malware Check** | Queries osv.dev for MAL-* advisories | `openhands/agenthub/extension_malware_check.rs` |
| **Extension Management Gating** | Always requires approval | `openhands/permission/permission_inspector.py` |

### UX & Developer Experience

| Capability | Implementation | File Path |
|------------|----------------|-----------|
| **Web UI** | React 19 + TypeScript + FastAPI | `frontend/`, `openhands/server/` |
| **Docker-Based Workflow** | Default containerized execution | `docker-compose.yml`, `containers/` |
| **Kubernetes Deployment** | Production K8s runtime | `openhands/runtime/impl/kubernetes/`, `kind/` |
| **BrowserGym Integration** | Playwright-based browser automation | `openhands/runtime/browser/`, `openhands/agenthub/browsing_agent/` |
| **Makefile** | 370 lines of build targets | `Makefile` |
| **TanStack Query** | Data fetching and caching | `frontend/src/hooks/` |
| **15+ Languages** | i18n support | `frontend/public/locales/` |

### Unique/Novel Features

| Feature | Description | File Path |
|---------|-------------|-----------|
| **9 Condenser Strategies** | Most sophisticated context management | `openhands/memory/condenser/impl/` |
| **Docker Sandbox by Default** | Most secure execution model | `openhands/runtime/impl/docker/` |
| **Event-Sourced Architecture** | Full event replay, time-travel debugging | `openhands/events/stream.py` |
| **StuckDetector** | 5 loop scenarios with recovery | `openhands/controller/stuck.py` |
| **Multi-Agent Delegation** | Shared event stream, separate state | `openhands/events/action/agent.py` |
| **BrowserGym Integration** | Research-grade browser automation | `openhands/runtime/browser/` |
| **9 Condenser Pipeline** | Composable strategies via `CondenserPipeline` | `openhands/memory/condenser/impl/pipeline.py` |
| **Micro-Agent System** | Trigger-based specialized agents | `openhands/memory/memory.py` |
| **Security Analyzer** | Three-pluggable backends | `openhands/security/` |

---

## Worth Stealing (for AVA)

### High Priority

1. **9 Condenser Strategies** (`openhands/memory/condenser/impl/`)
   - Most sophisticated context management
   - Recent, LLM-summarize, Amortized, Observation-masking, Structured, Hybrid, Browser-turn, Identity, No-op
   - Composable via `CondenserPipeline`

2. **Event-Sourced Architecture** (`openhands/events/stream.py`)
   - Full event replay capability
   - Time-travel debugging
   - Durable event log

3. **StuckDetector** (`openhands/controller/stuck.py`)
   - 5 loop scenarios: repeated pairs, errors, monologues, alternating, context window
   - `LoopRecoveryAction` with 3 strategies

### Medium Priority

4. **Docker Sandbox by Default** (`openhands/runtime/impl/docker/`)
   - Most secure execution model
   - Should be AVA's default

5. **Multi-Agent Delegation** (`openhands/events/action/agent.py`)
   - `AgentDelegateAction`/`AgentDelegateObservation`
   - Shared event stream, separate state

6. **BrowserGym Integration** (`openhands/runtime/browser/`)
   - Research-grade browser automation
   - Better than basic Puppeteer

7. **Security Analyzer Subsystem** (`openhands/security/`)
   - Three backends: Invariant, LLM, GraySwan
   - Defense in depth

### Lower Priority

8. **Micro-Agent System** — Specialized agents for specific tasks
9. **Kubernetes Runtime** — Only needed for cloud deployments
10. **Web UI** — AVA is desktop-native

---

## AVA Already Has (or Matches)

| OpenHands Feature | AVA Equivalent | Status |
|-------------------|----------------|--------|
| Docker sandbox | Docker sandbox extension | ✅ Parity |
| Context compaction | Token compaction | ✅ Partial (should add 9 strategies) |
| Multi-agent | Praxis 3-tier hierarchy | ✅ Better |
| Browser automation | Via MCP | ✅ Parity |
| Event-sourced | DAG session structure | ✅ Different approach |
| 9 condensers | 1 strategy | ❌ Gap |
| Stuck detection | Doom loop extension | ⚠️ Should upgrade to 5-scenario |
| Security analyzers | Middleware pipeline | ⚠️ Should add pluggable analyzers |

---

## Anti-Patterns to Avoid

1. **V0/V1 Migration Confusion** — Currently transitioning architectures; avoid major rewrites
2. **Web UI Complexity** — Full web stack adds overhead; desktop app is simpler
3. **9 Condenser Complexity** — May be overkill; start with 3-4 strategies
4. **Makefile Proliferation** — 370 lines is large; prefer npm scripts
5. **Python-Only** — Limits extension ecosystem; TypeScript is more accessible

---

## Recent Additions (Post-March 2026)

Based on git log analysis:

- **V1 Architecture** — New app server in `openhands/app_server/`
- **Enhanced Condensers** — Better browser output handling
- **Improved Security Analyzers** — Better GraySwan integration
- **Kubernetes Improvements** — Better K8s runtime

---

## File Reference Index

| File | Lines | Purpose |
|------|-------|---------|
| `openhands/memory/condenser/condenser.py` | ~200 | Condenser framework |
| `openhands/memory/condenser/impl/` | ~1,500 | 9 condenser strategies |
| `openhands/events/stream.py` | ~400 | Event-sourced architecture |
| `openhands/controller/agent_controller.py` | ~1,200 | Agent orchestrator |
| `openhands/controller/stuck.py` | ~200 | Stuck detection |
| `openhands/runtime/impl/docker/docker_runtime.py` | ~300 | Docker sandbox |
| `openhands/security/` | ~500 | Security analyzers |
| `openhands/memory/view.py` | ~150 | View abstraction |

---

*Audit generated by subagent analysis across 6 dimensions: Edit System, Context & Memory, Agent Loop, Safety, UX, and Unique Features.*
