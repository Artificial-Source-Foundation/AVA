# SWE-agent Deep Architecture & Tools Analysis

> Comprehensive competitive intelligence analysis of the SWE-agent codebase (v1.1.0).
> Princeton/Stanford research agent for automatically fixing GitHub issues. ~14k GitHub stars.
> Source: `docs/reference-code/swe-agent/`

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Layer 1: Agent Layer](#2-layer-1-agent-layer)
3. [Layer 2: Environment Layer](#3-layer-2-environment-layer)
4. [Layer 3: Tools Layer](#4-layer-3-tools-layer)
5. [Layer 4: Run Layer](#5-layer-4-run-layer)
6. [Tool Bundles: Deep Dive](#6-tool-bundles-deep-dive)
7. [Configuration System](#7-configuration-system)
8. [Key Innovations & Patterns](#8-key-innovations--patterns)
9. [Competitive Advantages](#9-competitive-advantages)
10. [Lessons for AVA](#10-lessons-for-ava)

---

## 1. Architecture Overview

SWE-agent uses a clean 4-layer architecture:

```
┌─────────────────────────────────────────────────────┐
│  Run Layer (Orchestration)                          │
│  RunSingle / RunBatch + ThreadPoolExecutor          │
├─────────────────────────────────────────────────────┤
│  Agent Layer (Intelligence)                         │
│  DefaultAgent / RetryAgent / ShellAgent             │
│  + History Processors + Action Samplers + Reviewers │
├─────────────────────────────────────────────────────┤
│  Tools Layer (ACI - Agent-Computer Interface)       │
│  Tool Bundles + 11 Parsers + Command Blocking       │
├─────────────────────────────────────────────────────┤
│  Environment Layer (Execution)                      │
│  SWEEnv → SWE-ReX → Docker Containers              │
└─────────────────────────────────────────────────────┘
```

### Why This Architecture Exists

The layered design solves a fundamental research problem: **how do you systematically test different agent strategies, tool sets, and LLMs across thousands of benchmark instances?** Each layer can be independently swapped via YAML configuration, enabling rapid experimentation.

---

## 2. Layer 1: Agent Layer

### 2.1 DefaultAgent (`sweagent/agent/agents.py`)

The core agent loop — ~1295 lines total in the file.

**What it does**: Runs the observe-think-act loop. Each turn: constructs messages from history, calls LLM, parses response into action, executes action in environment, records observation.

**Why it exists**: This is the minimal viable agent. But its real value is in the *details*:

- **Retry on format error**: If the LLM outputs something unparseable (e.g., no tool call, bad XML), the agent sends an error message back and tries again. Up to `max_requeries` attempts. This is critical because even Claude-3.5 occasionally produces malformed output.
- **Autosubmission on any failure**: If the agent crashes, hits cost limit, or times out, it *still* tries to extract `git diff` and submit. This means even partial solutions get evaluated. Research insight: many "failed" runs actually produce useful patches.
- **Configurable observation length**: `max_observation_length` truncates tool output. When truncated, a special template shows head + tail + elided character count. Without this, a single `cat` of a large file would blow the context window.
- **Demonstration support**: Can inject full example trajectories into the prompt for few-shot learning. The trajectory files are real recorded agent sessions.

### 2.2 RetryAgent (`sweagent/agent/reviewer.py`)

**What it does**: Wraps DefaultAgent to run multiple complete attempts at solving a problem, then uses a separate "reviewer" LLM to score or choose the best attempt.

**Why it exists**: LLM output is stochastic. Running 3 attempts and picking the best one significantly improves benchmark scores. Two strategies:

1. **ScoreRetryLoop**: Each attempt is scored independently (0-10) by a reviewer LLM. Highest score wins.
2. **ChooserRetryLoop**: The reviewer sees all attempts simultaneously and picks the best one.

**What would break without it**: Single-attempt success rate on SWE-bench is ~30-50%. Multi-attempt with review pushes significantly higher. This is SWE-agent's primary benchmark optimization.

### 2.3 Action Samplers (`sweagent/agent/action_sampler.py`)

**What they do**: Sample multiple LLM completions for a *single step*, then use a judge to pick the best one.

Two strategies:
- **BinaryTrajectoryComparison**: Sample N completions, run tournament brackets, judge picks winner at each stage.
- **AskColleagues**: Sample N completions, send all to a judge LLM with a "which is best?" prompt.

**Why they exist**: Best-of-N sampling is the cheapest way to improve quality. Instead of running N full trajectories (expensive), you run N single-step predictions (cheap) and pick the best. The key insight: most agent failures come from one bad decision, not consistently poor decisions.

**What would break without them**: Per-step decision quality drops. Critical for hard problems where one wrong edit cascades into unfixable states.

### 2.4 History Processors (`sweagent/agent/history_processors.py`)

Chainable processors that transform the message history before each LLM call:

| Processor | What it Does | Why it Exists |
|-----------|-------------|---------------|
| **LastNObservations** | Keeps only last N tool outputs | Prevents context window overflow on long runs |
| **ClosedWindow** | Replaces old observations with "[observation truncated]" | Preserves message structure while reducing tokens |
| **CacheControl** | Adds Anthropic cache_control markers to last N messages | **Saves 90% on repeated prefix tokens** with Claude's prompt caching |
| **RemoveRegex** | Strips patterns from observations | Remove ANSI codes, irrelevant warnings |
| **ImageParsing** | Converts base64 images to multimodal message format | Required for screenshot/image tools |

**Why they exist**: A 150-step agent run generates enormous context. Without history management, you either hit the context window limit or pay absurd API costs. CacheControl alone can reduce per-turn cost by 90% on Anthropic models.

**What would break without them**: Runs would fail at ~30-40 steps when the context window fills. Cost per instance would be 5-10x higher.

### 2.5 Models (`sweagent/agent/models.py`)

- **LiteLLM integration**: Supports any LiteLLM-compatible model (OpenAI, Anthropic, local, etc.)
- **HumanModel**: A human can play the role of the LLM — used for demonstrations and debugging
- **ReplayModel**: Replays a recorded trajectory — used for testing and reproducibility
- **Thread-aware API key rotation**: For batch runs, API keys from a comma-separated list are distributed by thread index to maximize prompt cache hits (each thread consistently uses the same key)
- **Cost tracking**: Per-instance and total cost limits with automatic early termination
- **Retry with exponential backoff**: Up to 6 retries with max 30s wait

**Why thread-aware key rotation matters**: Anthropic's prompt caching is per-API-key. If thread 1 uses key A consistently, its repeated prompts cache. If threads randomly pick keys, no caching occurs. This is a subtle but impactful optimization.

---

## 3. Layer 2: Environment Layer

### 3.1 SWEEnv (`sweagent/environment/swe_env.py`)

**What it does**: Wraps SWE-ReX (a separate Docker deployment system) to provide a sandboxed bash environment per problem instance.

**Key responsibilities**:
- Clone/reset repositories to the correct commit
- Apply test patches (gold patches for evaluation)
- Execute commands via persistent bash sessions
- Manage environment variables and working directory
- Install dependencies

**Why it exists**: Every SWE-bench instance requires a specific repo at a specific commit with specific dependencies. Without SWEEnv, you'd need to manually set up environments for thousands of instances.

**What would break without it**: No reproducibility. No sandboxing. One instance's changes would pollute another's.

### 3.2 Repository Configs (`sweagent/environment/repo.py`)

Five repo types:
1. **GitHubRepoConfig**: Clone from GitHub URL + commit
2. **LocalRepoConfig**: Use a local directory
3. **PreExistingRepoConfig**: Already inside the container
4. **SWESmithConfig**: For SWE-Smith synthetic benchmarks
5. **AutoRepoConfig**: Auto-detect from problem statement

**Why multiple types**: Flexibility for different use cases — benchmarking (GitHub), interactive use (local), CI/CD (pre-existing).

---

## 4. Layer 3: Tools Layer

### 4.1 Tool Handler (`sweagent/tools/tools.py`)

**What it does**: Manages the complete lifecycle of tools — loading bundles, generating documentation, blocking dangerous commands, executing tools in the environment.

**Key features**:
- **Command blocking**: Configurable list of blocked commands (e.g., `vim`, `nano`, `python -i`) that would hang in non-interactive environments
- **Command execution timeout**: Default 300s per command
- **Retry-with-output token**: When a tool outputs `###SWE-AGENT-RETRY-WITH-OUTPUT###`, the observation is fed back but the turn doesn't count as a new step. This lets edit tools reject bad edits without wasting a turn.
- **State commands**: After each tool execution, state commands run to update `/root/state.json`, which Jinja2 templates can reference

**Why blocking matters**: A single `vim` or `python` REPL call in a Docker container with no TTY hangs forever. The agent loses its remaining budget. Command blocking prevents this class of failure entirely.

### 4.2 Bundle System (`sweagent/tools/bundle.py`)

**What it does**: Packages shell scripts as deployable tool bundles.

```
bundle_name/
├── config.yaml      # Tool signatures, docstrings, arguments
├── bin/             # Executable scripts (bash or python)
├── lib/             # Shared libraries
└── install.sh       # Optional setup script (pip install, etc.)
```

**How it works**:
1. At session start, bundle directories are uploaded to the Docker container
2. `bin/` directories are added to PATH
3. `install.sh` scripts run to set up dependencies
4. `config.yaml` defines tool signatures for the LLM

**Why it exists**: Decouples tool implementation from the agent framework. You can add a new tool by creating a directory — no Python code changes needed. This enables rapid experimentation with different tool sets.

**What would break without it**: Every tool change would require modifying core agent code. Experimentation velocity would drop dramatically.

### 4.3 Parsing System (`sweagent/tools/parsing.py`)

**11 different output parsers** — this is unusually comprehensive:

| Parser | Format | Why it Exists |
|--------|--------|---------------|
| **FunctionCalling** | Native LLM function calls | Best for Claude/GPT-4 — uses native tool calling |
| **ThoughtAction** | `DISCUSSION\n```\ncommand\n```\n` | Original SWE-agent format, works with any LLM |
| **ThoughtActionXML** | `<thought>...<action>...` | Structured, less ambiguous than backticks |
| **JSONThoughtAction** | JSON with thought + command | Strict parsing for models that handle JSON well |
| **ActionOnly** | Just the command | For simple, instruction-following models |
| **XMLThoughtAction** | XML with separate tags | Alternative XML format |
| **ThoughtActionThought** | Discussion, command, more discussion | Allows post-action reasoning |
| **SingleBashCodeBlock** | Single ```bash``` block | For the bash-only config |
| **SingleBashCodeBlockAlternative** | Alternative bash block format | Fallback parser |
| **EditWhisperer** | Specialized edit format | For models that struggle with edit syntax |
| **Identity** | Pass-through | Raw output, no parsing |

**Why 11 parsers**: Different LLMs produce different output formats. Claude excels with function calling, while open-source models may need ThoughtAction. Having multiple parsers means you can use SWE-agent with *any* LLM without format training.

**What would break without them**: Locked to one LLM family or one output format. Research flexibility destroyed.

### 4.4 Heredoc Guard (`sweagent/tools/utils.py`)

**What it does**: Automatically wraps multi-line commands in heredoc syntax before sending to bash.

**Why it exists**: When an LLM generates:
```
edit 1:5
def hello():
    print("world")
end_of_edit
```
This can't be sent as a single bash command. The heredoc guard wraps it as:
```bash
edit 1:5 << 'END_OF_EDIT'
def hello():
    print("world")
END_OF_EDIT
```

**What would break without it**: Multi-line edits would fail. Shell escaping issues would corrupt code. This is a deceptively critical piece of infrastructure.

---

## 5. Layer 4: Run Layer

### 5.1 RunSingle (`sweagent/run/run_single.py`)

**What it does**: Orchestrates a single problem-solving session: set up environment → create agent → run loop → collect results.

**Key features**:
- Saves trajectories (full interaction history) for debugging and demonstrations
- Extracts patches from submissions
- Handles all error types gracefully (cost limit, timeout, crash) with autosubmission
- Hook system for extensibility (logging, metrics, etc.)

### 5.2 RunBatch (`sweagent/run/run_batch.py`)

**What it does**: Runs multiple instances in parallel using ThreadPoolExecutor.

**Key features**:
- Configurable number of workers
- Per-instance error isolation (one crash doesn't kill the batch)
- Progress tracking with tqdm
- Instance shuffling for balanced API load
- Resume support (skip already-completed instances)

**Why it exists**: SWE-bench has 2294 instances. Running them sequentially would take weeks. Batch parallelism with 20 workers cuts this to hours.

---

## 6. Tool Bundles: Deep Dive

### 6.1 Registry Bundle

**Tools**: None (infrastructure only)
**Files**: `lib/registry.py`, `bin/_read_env`, `bin/_write_env`

**What it does**: Provides `EnvRegistry` — a JSON-file-based key-value store at `/root/.swe-agent-env`.

**Why it exists**: In Unix, a child process cannot modify its parent's environment variables. When tool A (a subprocess) needs to tell tool B (another subprocess) which file is currently open, environment variables don't work. The registry persists state to a JSON file that all tools can read/write.

**What it stores**: `CURRENT_FILE`, `FIRST_LINE`, `WINDOW`, `OVERLAP`, `ROOT`, `file_history`, `SUBMIT_STAGE`, `SUBMIT_REVIEW_MESSAGES`, etc.

**What would break without it**: No cross-tool state. The windowed editor couldn't remember which file is open. Edit tools couldn't remember undo history. The entire stateful tool system collapses.

**Clever pattern**: `get_if_none(value, key, default)` — if value is explicitly provided, use it; otherwise look up in registry; otherwise use default. This enables both programmatic and stateful usage of the same tools.

### 6.2 Windowed File Viewer Bundle

**Tools**: `open`, `goto`, `create`, `scroll_up`, `scroll_down`
**State command**: `_state` (writes open_file + working_dir to `/root/state.json`)

**What it does**: Presents files through a fixed-size scrolling window (default 100 lines). The LLM sees a "viewport" into the file with line numbers, file path, and indicators for lines above/below.

**Why it exists**: LLMs cannot effectively navigate files by reading them entirely (context window waste) or by random `cat` commands (loses track of position). The windowed model:
1. Constrains observation size to `WINDOW` lines per tool call
2. Maintains persistent state (which file, which position)
3. Shows contextual cues (line numbers, "N more lines above/below")

**Core class** — `WindowedFile` (315 lines):
- All line numbers 0-indexed internally, displayed 1-indexed
- `offset_multiplier = 1/6` — positions goto target slightly below top of window for context
- `overlap` parameter prevents disorientation during scrolling (overlap lines shown on both pages)
- `undo_edit()` restores both text AND window position

**What would break without it**: LLMs would waste turns running `cat` on large files, lose track of position, and exhaust context windows. The windowed model is what makes file editing tractable for LLMs.

### 6.3 Edit Bundle: Anthropic Style (`edit_anthropic`)

**Tools**: `str_replace_editor` (with subcommands: view, create, str_replace, insert, undo_edit)
**State command**: `_state_anthropic`

**This is the primary edit tool in the default (best-performing) configuration.**

**What it does**: A stateless file editor inspired by Anthropic's computer-use demo. Key operations:

- **str_replace**: Find exact text → replace with new text. Requires uniqueness (exits if 0 or >1 matches). This is THE core editing primitive.
- **view**: Shows file content. For large Python files, automatically uses tree-sitter filemap (shows structure with function bodies elided).
- **insert**: Insert text after a specific line number.
- **create**: Create new file with content.
- **undo_edit**: Revert last edit per file.

**Why str_replace over line-range editing**:
1. **Robust to insertion/deletion**: Line numbers shift after edits. str_replace uses content matching, which is stable.
2. **Self-verifying**: The LLM must reproduce the exact existing code to replace it, proving it understood the current state.
3. **Unique match requirement**: Forces the LLM to include enough context to be unambiguous.

**Filemap integration** (710-line `str_replace_editor` script):
- When viewing a large Python file (>16000 chars), automatically shows a tree-sitter-generated structural map
- Function bodies ≥5 lines are elided, showing only signatures
- This gives the LLM a "table of contents" to understand file structure before diving into specific sections

**WindowExpander** — smart viewport expansion:
- When showing edit results, expands the viewport to natural breakpoints (function/class definitions, blank lines)
- Up to 30 lines in each direction
- Python-aware: recognizes `def`, `class`, `@decorator` as strong breakpoints
- This means edit confirmations show complete functions, not arbitrary line ranges

**Linting integration**:
- Runs flake8 before AND after each edit
- Compares errors, filtering pre-existing ones (with line-number adjustment for edits that shift content)
- Only reports NEW errors introduced by the edit
- Specific error codes: F821 (undefined name), F822 (undefined export), F831 (duplicate argument), E111-E113 (indentation), E999 (syntax), E902 (IO error)

**What would break without it**: Edit accuracy drops significantly. Without uniqueness enforcement, LLMs make ambiguous edits. Without filemap, they waste turns reading entire files. Without linting, syntax errors propagate silently.

### 6.4 Edit Bundle: Windowed with Linting (`windowed_edit_linting`)

**Tools**: `edit <start_line>:<end_line>`
**Used in**: SWE-agent 0.7 configuration

**What it does**: Replaces a line range in the currently open file. Reads replacement text until `end_of_edit` sentinel.

**Key innovation — LINT GATING**:
- If flake8 detects new syntax errors, **the edit is REVERTED**
- The agent sees the rejected edit, the original code, AND the error messages
- The agent is told: "Your changes have NOT been applied. DO NOT re-run the same failed edit command."

**Why lint gating matters**: Without it, an agent can introduce a syntax error on step 5 and spend the next 40 steps trying to fix cascading failures. Lint gating catches errors at introduction time, saving enormous compute.

**What would break without it**: Agents introduce syntax errors ~15% of the time. Each error costs 3-10 additional turns to fix. Over thousands of instances, this is a massive efficiency loss.

### 6.5 Edit Bundle: Search-Replace (`windowed_edit_replace`)

**Tools**: `edit <search> <replace> [<replace-all>]`, `insert <text> [<line>]`

**What it does**: Search-and-replace within the currently visible window (or whole file with replace-all).

**Unique features**:
- **RETRY_WITH_OUTPUT_TOKEN**: `###SWE-AGENT-RETRY-WITH-OUTPUT###` — when an edit fails (not found, ambiguous, same text), this token tells the agent framework to show the error but NOT count it as a wasted turn. The agent gets another chance.
- **Window-scoped search**: By default only searches in currently displayed lines, preventing accidental global replacements
- **Helpful error messages**: When text isn't found in window but exists elsewhere, shows line numbers of occurrences so the agent can `goto` them

**Why window-scoped**: Prevents a search-replace from modifying code the agent hasn't reviewed. Forces the agent to look at what it's changing.

### 6.6 Edit Bundle: Window Rewrite (`windowed_edit_rewrite`)

**Tools**: `edit <text>`

**What it does**: Replaces the ENTIRE currently displayed window with new text.

**Why it exists**: For small, contained changes, rewriting the whole visible section is simpler than search-replace. Useful for models that struggle with the search-replace abstraction.

**Trade-off**: More token-expensive (must reproduce unchanged lines) but less error-prone (no matching failures).

### 6.7 Search Bundle

**Tools**: `find_file`, `search_dir`, `search_file`

**find_file**: Finds files by name pattern (shell glob). Uses `find -type f -name`.
**search_dir**: Greps across all non-hidden files in a directory. Groups results by file with match count. **Errors if >100 files match** — prevents overwhelming output.
**search_file**: Greps in a specific file or the currently open file. Shows line numbers. **Errors if >100 lines match**.

**Why the 100-match limit**: LLMs cannot process hundreds of search results effectively. By capping output, the tool forces more specific queries, which produce more useful results.

**What would break without limits**: An agent searching for `import` in a large repo gets thousands of results, blowing the context window and providing no useful signal.

### 6.8 Filemap Bundle

**Tools**: `filemap <file_path>`

**What it does**: Uses tree-sitter to parse Python files, showing class/function signatures with bodies elided (bodies ≥5 lines become "... eliding lines X-Y ...").

**Why it exists**: Gives the LLM a structural overview of any Python file in one shot. Without it, the LLM must scroll through entire files to understand structure — costing 5-20 turns.

**What would break without it**: Navigation efficiency plummets. The LLM spends turns exploring file structure instead of solving the problem.

### 6.9 Submit Bundle

**Tools**: `submit`

**What it does**: Captures `git diff --cached` and outputs it between `<<SWE_AGENT_SUBMISSION>>` markers. Also reverses any test patches before generating the diff.

**Why markers exist**: The agent framework looks for these markers to extract the final patch. They're unambiguous delimiters that won't appear in normal code.

**Why reverse test patches**: SWE-bench provides gold test patches. The agent should only submit its own changes, not the test setup.

### 6.10 Review-on-Submit Bundle (`review_on_submit_m`)

**Tools**: `submit` (overrides the basic submit)

**What it does**: Implements multi-stage review before actual submission.

**How it works**:
1. Agent calls `submit`
2. Instead of submitting, it shows a review message: "Please check X, Y, Z. Here's your diff: {{diff}}"
3. Agent reviews and can make more changes
4. Agent calls `submit` again — this time it actually submits (or there are more review stages)
5. Only `submit -f` (force) bypasses review — used by the framework for autosubmission on error

**Configurable via SUBMIT_REVIEW_MESSAGES** — a list of review prompts. Default asks the agent to:
- Re-run reproduction script
- Remove reproduction script
- Revert any test file modifications
- Review the full diff

**Why it exists**: Agents frequently submit with:
- Leftover debug print statements
- Modified test files (which SWE-bench evaluates separately)
- Forgotten reproduction scripts in the repo

Review-on-submit catches these issues. **This directly improves benchmark scores.**

### 6.11 Diff State Bundle

**Tools**: None (state-only)
**State command**: `_state_diff_state`

**What it does**: After every tool call, runs `git add -A && git diff --cached` and stores the result in `/root/state.json` under the `diff` key.

**Why it exists**: Makes the current diff available to prompt templates via `{{diff}}`. The review-on-submit bundle uses this to show the agent its changes.

**What would break without it**: The review system couldn't show the agent its own diff. The agent would have to manually run `git diff` to see its changes.

### 6.12 Forfeit Bundle

**Tools**: `exit_forfeit`

**What it does**: Outputs `###SWE-AGENT-EXIT-FORFEIT###` sentinel, which the agent loop detects as a signal to terminate gracefully.

**Why it exists**: Allows the agent to decide a problem is unsolvable and stop, rather than burning remaining budget on futile attempts.

### 6.13 Image Tools Bundle

**Tools**: `view_image`

**What it does**: Base64-encodes an image file (png/jpeg/webp) and outputs it as a markdown data URI: `![path](data:image/png;base64,...)`.

**Why it exists**: Enables multimodal LLMs to see images in the repo (screenshots, diagrams, etc.). The ImageParsing history processor converts these to proper multimodal message format.

### 6.14 Web Browser Bundle

**Tools**: 15 browser automation tools (open_site, screenshot_site, click_mouse, type_text, scroll, execute_script, navigate_back/forward, etc.)

**Implementation**: Playwright-based BrowserManager with:
- Thread-safe operations (RLock)
- Lazy browser initialization
- **Red crosshair injection** — injects a visible crosshair at the mouse position in screenshots so the LLM can see where the cursor is
- Console message capture for debugging JavaScript
- Key mapping with 130+ aliases
- Configurable window size and screenshot delay

**Why it exists**: Enables SWE-agent to tackle web-based tasks — testing web apps, navigating documentation, debugging frontend issues.

### 6.15 Multilingual Setup Bundle

**Tools**: None (setup-only)
**Install script only**

**What it does**: Sets up Python 3.11 if no python exists in the container, imports environment variables from the Docker init process (`/proc/1/environ`), merges PATH.

**Why it exists**: SWE-bench primarily tests Python repos, so containers are Python-optimized. For non-Python repos (JavaScript, Java, C++), this bundle ensures a working Python installation (needed by the Python-based tools) while preserving the original language environment.

---

## 7. Configuration System

Configurations compose the system via YAML:

```yaml
agent:
  type: default | retry
  templates:
    system_template: "..."      # System prompt
    instance_template: "..."    # Problem prompt (Jinja2 with {{state}} vars)
    next_step_template: "..."   # Observation format
  tools:
    bundles:                    # Which tool bundles to load
      - path: tools/registry
      - path: tools/edit_anthropic
    env_variables:              # Shell environment vars
      PAGER: cat
    registry_variables:         # EnvRegistry vars
      USE_FILEMAP: 'true'
    enable_bash_tool: true      # Allow raw bash commands
    parse_function:
      type: function_calling    # One of 11 parser types
  history_processors:
    - type: cache_control
      last_n_messages: 2
  model:
    name: claude-3-7-sonnet-20250219
    per_instance_cost_limit: 2
    per_instance_call_limit: 150
    temperature: 0.0
```

### Notable Configurations

| Config | Tools | Parser | Use Case |
|--------|-------|--------|----------|
| **default.yaml** | edit_anthropic + review_on_submit | function_calling | Best performance (Claude) |
| **bash_only.yaml** | submit only | single_bash_code_block | Any LLM, no custom tools |
| **coding_challenge.yaml** | windowed + search + edit_replace | thought_action | LeetCode-style problems |
| **07.yaml** | windowed + search + edit_linting | thought_action | Legacy SWE-agent 0.7 |
| **benchmark configs** | edit_anthropic + diff_state + review | function_calling | SWE-bench evaluation |

### Configuration Evolution

SWE-agent's history shows a clear evolution in tool design philosophy:

1. **v0.7**: Windowed editor + search + lint-gated editing. ThoughtAction format.
2. **Current default**: Anthropic str_replace_editor + filemap + review-on-submit. Function calling.
3. **Benchmark configs**: Add diff_state for review templates + cost/call limits.

The shift from windowed editing to str_replace reflects a key lesson: **content-addressed edits (search for text) are more robust than position-addressed edits (edit lines X-Y)** because line numbers shift after edits.

---

## 8. Key Innovations & Patterns

### 8.1 State Command Architecture

After every tool execution, "state commands" run silently to update `/root/state.json`. Prompt templates (Jinja2) can reference this state.

```
Tool executes → State command runs → JSON updated → Next prompt uses {{state}}
```

This creates a **reactive state system**: the LLM always sees current environment state (open file, working directory, current diff) without wasting a turn to check it.

### 8.2 Retry-With-Output Token

When tools output `###SWE-AGENT-RETRY-WITH-OUTPUT###`, the observation is shown to the LLM but the turn counter doesn't increment. This means failed edits don't waste the turn budget.

**Why this is clever**: Without it, an edit failure costs one of your 150 turns. With 15% edit failure rate over 50 edits, that's ~7 wasted turns. The retry token recovers these.

### 8.3 Lint Gating (Edit Rejection)

All edit tools run flake8 before and after edits. If new syntax errors are introduced, the edit is REVERTED and the agent is shown both the failed edit and original code.

**The error diffing is sophisticated**: It adjusts previous error line numbers based on how the edit shifted lines, then filters out pre-existing errors. Only genuinely new errors are reported.

### 8.4 Autosubmission on Failure

On ANY exception (crash, timeout, cost limit), the agent attempts:
```python
try:
    # Extract git diff
    # Submit as patch
except:
    pass  # Even submission failure is swallowed
```

**Impact**: In SWE-bench evaluation, ~5-10% of "failed" runs actually produced correct patches that just hit cost limits or crashed for unrelated reasons. Autosubmission captures these.

### 8.5 EnvRegistry for Cross-Process State

The JSON file registry solves a fundamental Unix limitation: subprocesses can't modify parent environment variables. By persisting to `/root/.swe-agent-env`, any tool can share state with any other tool.

### 8.6 Filemap with Tree-Sitter

Using tree-sitter to parse Python and elide function bodies is a powerful context compression technique. A 500-line file might compress to 50 lines of signatures, giving the LLM structural understanding without context cost.

### 8.7 Window Expansion at Natural Breakpoints

When showing edit results, the WindowExpander finds natural breakpoints (function defs, class defs, decorators, blank lines) to expand the viewport. This means the LLM sees complete semantic units, not arbitrary line ranges.

### 8.8 Prompt-as-Configuration

Everything is configurable via YAML — system prompts, observation templates, truncation templates, error templates. This means prompt engineering is done in config files, not code. Research iterations are YAML-only changes.

---

## 9. Competitive Advantages

### 9.1 vs. OpenHands

| Aspect | SWE-agent | OpenHands |
|--------|-----------|-----------|
| **Tool design** | Shell scripts in bundles | Python AgentSkills |
| **Edit strategy** | str_replace (content-addressed) | Similar (adapted from SWE-agent) |
| **Lint gating** | Built into edit tools | Separate |
| **Multi-attempt** | RetryAgent with reviewer | Not built-in |
| **Action sampling** | Best-of-N with judge | Not built-in |
| **Extensibility** | Directory-based bundles | Python classes |
| **Configuration** | Comprehensive YAML | Code-heavy |

### 9.2 What SWE-agent Does Better Than Most

1. **Research velocity**: YAML-driven configuration means testing new strategies requires zero code changes
2. **Benchmark optimization**: Multi-attempt + review + autosubmission squeeze maximum benchmark performance
3. **Robust editing**: Multiple edit strategies with lint gating prevent cascading failures
4. **Context management**: History processors + filemap + window expansion minimize context waste
5. **Cost optimization**: CacheControl + API key rotation + cost limits make batch runs affordable
6. **Error recovery**: Every error path leads to either a retry or an autosubmission — nothing is wasted

### 9.3 Weaknesses

1. **Python-centric**: Filemap and linting are Python-specific (though multilingual_setup exists)
2. **Docker-dependent**: Requires SWE-ReX + Docker, heavy operational overhead
3. **No real-time streaming**: Batch-oriented, not designed for interactive use
4. **No multi-file awareness**: Tools operate on one file at a time
5. **No LSP integration**: Relies on grep/find for code navigation, not language-server intelligence

---

## 10. Lessons for AVA

### 10.1 Ideas to Adopt

1. **Lint gating on edits**: AVA should reject edits that introduce syntax errors. The before/after flake8 comparison with line-number adjustment is well-proven.

2. **State commands**: After each tool execution, silently update environment state that's available to prompt templates. This eliminates "check status" turns.

3. **Review-on-submit pattern**: Force the agent to review its changes before finalizing. The multi-stage review with diff injection directly improves output quality.

4. **Filemap with tree-sitter**: For large files, show structural overview first. This is cheaper and more effective than scrolling.

5. **Retry-with-output token**: Don't waste turn budget on failed tool calls. Let the agent retry without counting against its limit.

6. **History processors as a chain**: Composable processors for context management. CacheControl for Anthropic models is essentially free performance.

7. **Window expansion at breakpoints**: When showing code context, find natural boundaries (function/class definitions) instead of arbitrary line ranges.

8. **Autosubmission on error**: Never lose partial work. Even if the session crashes, save what was accomplished.

### 10.2 Ideas to Improve Upon

1. **Multi-language linting**: SWE-agent only lints Python. AVA should support TypeScript (ESLint), Rust (clippy), etc.

2. **LSP integration instead of grep**: AVA already has LSP — this is a significant advantage over SWE-agent's grep-based navigation.

3. **Multi-file editing**: AVA's `multiedit` tool is ahead of SWE-agent's one-file-at-a-time approach.

4. **Interactive streaming**: AVA is designed for real-time human interaction, not batch benchmarking. This is a different and complementary strength.

5. **MCP integration**: AVA's MCP support allows arbitrary tool extension without the constraints of the bundle system.

### 10.3 Architecture Patterns to Consider

1. **JSON file registry**: Simple but effective. AVA's platform abstraction could incorporate something similar for cross-tool state without relying on process environment.

2. **Bundle-style tool packaging**: While AVA uses `defineTool()`, the idea of self-contained tool packages with install scripts, configs, and shared libraries is worth considering for plugin tools.

3. **Multiple parser types**: Supporting different LLM output formats (function calling, XML, text) makes the system work with any provider.

4. **Configuration-driven experimentation**: AVA's config system should support as much prompt/tool composition without code changes as SWE-agent achieves.

---

## Appendix: Complete Tool Inventory

| Bundle | Tool | Type | Lines | Purpose |
|--------|------|------|-------|---------|
| registry | (infrastructure) | Python lib | 56 | Cross-process JSON state store |
| windowed | open | Python | 49 | Open file in windowed viewer |
| windowed | goto | Python | 37 | Jump to line |
| windowed | create | Python | 29 | Create new file |
| windowed | scroll_up | Python | 13 | Scroll up by window size |
| windowed | scroll_down | Python | 12 | Scroll down by window size |
| windowed | _state | Python | 25 | Update state.json with open file info |
| windowed | (lib) WindowedFile | Python | 315 | Core windowed file abstraction |
| edit_anthropic | str_replace_editor | Python | 710 | Anthropic-style editor with filemap + linting |
| edit_anthropic | _state_anthropic | Python | 21 | Update state.json with working dir |
| windowed_edit_linting | edit | Python | 128 | Line-range editor with lint gating |
| windowed_edit_replace | edit | Python | 172 | Search-replace editor with lint gating |
| windowed_edit_replace | insert | Python | (shared) | Insert text at line |
| windowed_edit_rewrite | edit | Python | 78 | Full window rewrite with lint gating |
| search | find_file | Bash | 31 | Find files by name/glob |
| search | search_dir | Bash | 39 | Grep across directory |
| search | search_file | Bash | 55 | Grep in single file |
| filemap | filemap | Python | 45 | Tree-sitter structural overview |
| submit | submit | Bash | 17 | Git diff submission |
| review_on_submit_m | submit | Python | 54 | Multi-stage review before submit |
| diff_state | _state_diff_state | Python | 52 | Track git diff as state |
| forfeit | exit_forfeit | Bash | 5 | Graceful termination |
| image_tools | view_image | Python | 36 | Base64 image viewing |
| web_browser | 15 tools | Python | 326+ | Playwright browser automation |
| multilingual_setup | (setup) | Bash | 45 | Non-Python language support |
