# Aider

> Python-based terminal AI coding assistant (~41k GitHub stars)
> Analyzed: March 2026

---

## Architecture Summary

Aider is a **monolithic Python CLI tool** with no plugin system or modular architecture. Everything lives in a single package (~20K lines across ~50 files) with a flat namespace. The core is the `Coder` class in `base_coder.py` (~2,500 lines) which contains the entire agent loop, streaming, tool execution routing, and context management.

**Key architectural decisions:**
- **Chat-and-parse** architecture (not tool-calling) — LLM produces markdown blocks that are parsed client-side
- **Strategy pattern for edit formats** — 12 different edit format implementations (SEARCH/REPLACE, whole file, unified diff, etc.)
- **Three-model architecture** — Main model for edits, weak model for summaries/commits, editor model for architect mode
- **Git-native design** — Mandatory git integration with auto-commits after every edit

### Project Structure

```
aider/
├── aider/
│   ├── coders/              # Edit format implementations (~38 files)
│   │   ├── base_coder.py    # Core agent loop (2,000+ lines)
│   │   ├── editblock_coder.py   # SEARCH/REPLACE blocks
│   │   ├── wholefile_coder.py   # Full file replacement
│   │   ├── architect_coder.py   # Two-model pipeline
│   │   └── search_replace.py    # Fuzzy matching cascade
│   ├── repomap.py           # PageRank-based repository mapping
│   ├── repo.py              # Git integration
│   ├── commands.py          # 40+ slash commands
│   └── models.py            # Model configuration (~1,400 lines)
```

---

## Key Patterns

### 1. Multi-Strategy Edit Cascade

Aider's most famous innovation is its **12-strategy fuzzy matching pipeline** for SEARCH/REPLACE blocks:

```
Exact match → Trailing whitespace stripping → Leading whitespace normalization →
Relative indentation → Git cherry-pick merge → diff-match-patch →
Cross-file matching → "Did you mean?" suggestions
```

This cascade achieves ~95% edit success rate even when LLMs produce imperfect output.

### 2. PageRank Repository Map

Uses tree-sitter + NetworkX to build a code dependency graph, then applies **personalized PageRank** biased toward:
- Files currently in the chat (50x boost)
- Files mentioned in user message (10x boost)
- Long identifiers with specific names (10x boost)

Uses binary search to fit the ranked tags within a token budget (default 1024, 8x when no files in chat).

### 3. Edit Format Strategy Pattern

Each edit format is a `Coder` subclass with its own system prompts, examples, and parsing logic:

| Format | Best For |
|--------|----------|
| `diff` (SEARCH/REPLACE) | Most models, default |
| `whole` | Weak models, small files |
| `udiff` | Token-efficient but less reliable |
| `architect` | Two-model planner/editor pipeline |

Model-specific configurations stored in `model-settings.yml` (50+ models).

### 4. Three-Model Cost Optimization

| Role | Default | Purpose |
|------|---------|---------|
| Main | User's choice | Code editing |
| Weak | gpt-4o-mini | Commit messages, summarization |
| Editor | Model-specific | Architect mode implementation |

Cost savings: ~55% vs single-model approach for typical sessions.

---

## What AVA Can Learn

### High Priority

1. **PageRank Repo Map** — Implement tree-sitter + graph analysis for intelligent context selection. This is Aider's single most important feature for code quality.

2. **Multi-Strategy Edit Cascade** — Adopt the 12-strategy cascade (exact → fuzzy → git merge → dmp). AVA currently uses simpler matching.

3. **Per-Model Configuration** — Model-specific prompts, edit formats, and parameters are essential for supporting 100+ models reliably.

### Medium Priority

4. **Architect Mode** — Two-model pipeline (strong planner + cheap editor) for complex tasks. Only 48 lines of code for significant quality improvement.

5. **Background Cache Warming** — Ping Anthropic models every 5 minutes to prevent cache expiration. ROI is ~4000:1 in cost savings.

6. **AI Comment Triggers** — `# AI!` / `# AI?` comments in source files auto-trigger the agent. Bridges CLI and IDE workflows.

### Patterns to Avoid

- **Monolithic architecture** — AVA's modular design is superior
- **No tool calling** — Limits autonomy; AVA's tool-calling approach is correct
- **No sandboxing** — Direct `subprocess.Popen(shell=True)` is risky
- **No MCP support** — Limits extensibility

---

## Comparison: Aider vs AVA

| Capability | Aider | AVA |
|------------|-------|-----|
| **Edit reliability** | 95% (12 strategies) | ~85% (8 strategies) |
| **Model support** | 100+ with tuning | ~16 providers |
| **Architecture** | Monolithic Python | Modular TypeScript/Rust |
| **Repo understanding** | PageRank graph | Tree-sitter symbols |
| **Cost optimization** | Three-model | Single model |
| **Autonomy** | Limited (no tools) | Full tool-calling |
| **Extensibility** | None | MCP + Extensions |
| **Platform** | CLI only | Desktop + CLI |

---

## File References

| File | Lines | Purpose |
|------|-------|---------|
| `aider/coders/base_coder.py` | 2,485 | Core agent loop |
| `aider/coders/search_replace.py` | ~400 | Fuzzy matching engine |
| `aider/repomap.py` | ~600 | PageRank repo map |
| `aider/models.py` | 1,323 | Model configuration |

---

*Consolidated from: audits/aider-audit.md, backend-analysis/aider.md, backend-analysis/aider-detailed.md*
