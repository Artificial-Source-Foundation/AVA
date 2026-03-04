# SWE-agent Backend Architecture Analysis

> Princeton/Stanford research agent for automatically fixing GitHub issues. ~14k GitHub stars. Version 1.1.0.

---

## 1. Project Structure

```
swe-agent/
├── sweagent/                    # Core Python package
│   ├── __init__.py              # Version (1.1.0), paths, SWE-ReX version check
│   ├── __main__.py              # Entry point
│   ├── types.py                 # Shared types (StepOutput, TrajectoryStep, History)
│   ├── exceptions.py            # Custom exceptions (FormatError, CostLimit, ContextWindow)
│   ├── agent/                   # Agent loop, models, parsing
│   │   ├── agents.py            # DefaultAgent, RetryAgent, ShellAgent configs (~1295 lines)
│   │   ├── models.py            # LiteLLM wrapper, HumanModel, ReplayModel (~904 lines)
│   │   ├── history_processors.py # Context management (LastN, CacheControl, etc.)
│   │   ├── problem_statement.py # Problem source types (GitHub, text, file, SWE-bench)
│   │   ├── reviewer.py          # Retry loop with scoring/choosing between attempts
│   │   ├── action_sampler.py    # Best-of-N sampling strategies
│   │   ├── hooks/               # Agent lifecycle hooks
│   │   └── extra/
│   │       └── shell_agent.py   # Human-AI collaborative shell mode
│   ├── environment/             # Sandboxed execution environment
│   │   ├── swe_env.py           # SWEEnv — Docker/deployment wrapper (~277 lines)
│   │   ├── repo.py              # Repo config (GitHub, local, pre-existing)
│   │   └── hooks/               # Environment lifecycle hooks
│   ├── tools/                   # Tool system (ACI)
│   │   ├── tools.py             # ToolConfig, ToolHandler — install, parse, block, state
│   │   ├── commands.py          # Command/Argument models, BASH_COMMAND builtin
│   │   ├── bundle.py            # Tool bundle loading from YAML + bash scripts
│   │   ├── parsing.py           # 10 output parsers (function_calling, thought_action, etc.)
│   │   └── utils.py             # Heredoc guards, command docs generation
│   ├── run/                     # CLI runners and orchestration
│   │   ├── run.py               # Main CLI dispatcher (sweagent <command>)
│   │   ├── run_single.py        # Single instance runner
│   │   ├── run_batch.py         # Batch runner with ThreadPoolExecutor
│   │   ├── run_replay.py        # Trajectory replay
│   │   ├── run_shell.py         # Interactive shell mode
│   │   ├── batch_instances.py   # Instance loading (SWE-bench, files, HuggingFace)
│   │   ├── hooks/               # Run hooks (apply_patch, open_pr, swe_bench_evaluate)
│   │   └── ...                  # Stats, progress, merge predictions
│   ├── inspector/               # Trajectory viewer (web + CLI)
│   └── utils/                   # Config loading, GitHub helpers, logging, serialization
├── tools/                       # Tool bundles (bash scripts + YAML configs)
│   ├── windowed/                # File viewer with scrolling window
│   ├── windowed_edit_linting/   # Line-range edit with linting
│   ├── windowed_edit_replace/   # Search-replace edit
│   ├── windowed_edit_rewrite/   # Full window rewrite
│   ├── edit_anthropic/          # Anthropic-style str_replace_editor
│   ├── search/                  # find_file, search_dir, search_file
│   ├── submit/                  # Submit command
│   ├── review_on_submit_m/      # Submit with self-review
│   ├── diff_state/              # Git diff state tracking
│   ├── filemap/                 # Python file structure viewer
│   ├── forfeit/                 # Give-up command
│   ├── image_tools/             # Image viewing
│   ├── web_browser/             # Playwright-based browser (16 tools)
│   ├── registry/                # Persistent key-value registry
│   └── multilingual_setup/      # Non-Python language setup
├── config/                      # YAML configuration presets
│   ├── default.yaml             # Anthropic-style (str_replace_editor + function calling)
│   ├── sweagent_0_7/            # Original SWE-agent configs (windowed + thought_action)
│   ├── benchmarks/              # SWE-bench specific configs
│   └── ...
├── trajectories/                # Saved demonstrations for few-shot
│   └── demonstrations/          # Pre-recorded trajectories
└── tests/                       # Pytest test suite
```

**Key design principle:** The codebase is ~5,000 lines of Python (excluding tools). It separates concerns cleanly: the agent loop (`agents.py`) orchestrates turns, the environment (`swe_env.py`) manages the sandbox, and tools are external bash scripts loaded from bundles.

**Dependencies:** Pydantic for config, LiteLLM for model abstraction, SWE-ReX for sandboxed execution, Jinja2 for templates, simple-parsing for CLI, tenacity for retries.

---

## 2. Tools/Actions — The Agent-Computer Interface (ACI)

SWE-agent's core innovation is the **Agent-Computer Interface (ACI)** — purpose-built tools that replace raw bash access. The key insight from their paper: *designing the right interface for the agent matters as much as the model*.

### Tool Bundle System

Tools are organized as **bundles** — directories containing:
- `config.yaml` — tool definitions (name, signature, arguments, docstring)
- `bin/` — executable scripts (Python or bash)
- `lib/` — shared libraries
- `install.sh` — optional setup script

**File:** `sweagent/tools/bundle.py`

Bundles are uploaded to the container at `/root/tools/<bundle_name>/bin/` and added to `$PATH`.

### Complete Tool Inventory

#### Core Builtin
| Tool | Source | Description |
|------|--------|-------------|
| `bash` | Built-in (`commands.py`) | Run arbitrary bash commands. Always enabled by default. |

#### Windowed File Viewer (`tools/windowed/`)
| Tool | Description |
|------|-------------|
| `open` | Open a file at optional line number, shows 100-line window with line numbers |
| `goto` | Jump to a specific line number |
| `scroll_up` | Move window up by WINDOW lines |
| `scroll_down` | Move window down by WINDOW lines |
| `create` | Create and open a new file |

State command `_state` tracks `open_file` and `working_dir` via a JSON registry file (`/root/state.json`).

#### Edit Variants

**Line-range edit** (`tools/windowed_edit_linting/`):
```
edit <start_line>:<end_line>
<replacement_text>
end_of_edit
```
Replaces lines inclusively. Uses heredoc-style multi-line input. Runs flake8 linting after edit.

**Search-replace edit** (`tools/windowed_edit_replace/`):
```
edit <search> <replace> [<replace-all>]
```
Plus `insert <text> [<line>]`.

**Full rewrite edit** (`tools/windowed_edit_rewrite/`):
```
edit <text>
```
Replaces entire displayed window content.

**Anthropic-style editor** (`tools/edit_anthropic/`):
```
str_replace_editor <command> <path> [args...]
```
Commands: `view`, `create`, `str_replace`, `insert`, `undo_edit`. This is the default in the current `config/default.yaml`. Modeled after Anthropic's computer use demo and OpenHands' implementation.

#### Search Tools (`tools/search/`)
| Tool | Signature | Description |
|------|-----------|-------------|
| `find_file` | `find_file <file_name> [<dir>]` | Find files by name/pattern (shell wildcards) |
| `search_dir` | `search_dir <search_term> [<dir>]` | Grep-like search across directory |
| `search_file` | `search_file <search_term> [<file>]` | Search within current open file |

#### Submit Tools
| Tool | Bundle | Description |
|------|--------|-------------|
| `submit` | `tools/submit/` | Submit the current patch |
| `submit` | `tools/review_on_submit_m/` | Submit with self-review messages (the default) |
| `exit_forfeit` | `tools/forfeit/` | Give up on the task |

#### Other Tools
| Tool | Bundle | Description |
|------|--------|-------------|
| `filemap` | `tools/filemap/` | Print Python file structure (skipping long bodies) |
| `view_image` | `tools/image_tools/` | View an image file |
| `_state_diff_state` | `tools/diff_state/` | Track git diff as environment state |

#### Web Browser Tools (`tools/web_browser/`) — 16 tools
`open_site`, `close_site`, `screenshot_site`, `click_mouse`, `double_click_mouse`, `move_mouse`, `drag_mouse`, `type_text`, `scroll_on_page`, `execute_script_on_page`, `navigate_back`, `navigate_forward`, `reload_page`, `wait_time`, `press_keys_on_page`, `set_browser_window_size`, `get_console_output`

### Tool Filtering/Blocking

**File:** `sweagent/tools/tools.py` — `ToolFilterConfig`

```python
blocklist = ["vim", "vi", "emacs", "nano", "nohup", "gdb", "less", "tail -f", "python -m venv", "make"]
blocklist_standalone = ["python", "python3", "ipython", "bash", "sh", "/bin/bash", "/bin/sh", "nohup", "vi", "vim", "emacs", "nano", "su"]
block_unless_regex = {"radare2": r"\b(?:radare2)\b.*\s+-c\s+.*", "r2": r"..."}
```

Interactive commands are blocked. Standalone interpreters (python, bash) are blocked to prevent nested shells. The `block_unless_regex` allows tools like radare2 only in non-interactive mode.

### Registry System

**File:** `tools/registry/lib/registry.py`

A persistent JSON-backed key-value store at `/root/.swe-agent-env`. Tools use this to share state (current file, window position) without environment variables (which don't persist across subprocesses). Written at tool install time with `registry_variables` from config.

---

## 3. Agent Loop

### Core Loop Architecture

**File:** `sweagent/agent/agents.py` — `DefaultAgent`

The agent uses a straightforward turn-based loop:

```
setup() → while not done: step() → save_trajectory()
```

#### `step()` method (line 1235):
1. Call `forward_with_handling(self.messages)` — handles all error recovery
2. Add step to history (`add_step_to_history`)
3. Update info dict (submission, exit status, model stats)
4. Add step to trajectory
5. Fire `on_step_done` hook

#### `forward()` method (line 1006):
1. Check total execution time limit
2. Query the model with processed history
3. Parse the response into (thought, action) via `tools.parse_actions()`
4. Execute the action via `handle_action()`
5. Check for submission markers in output

#### `forward_with_handling()` method (line 1062):
Wraps `forward()` with comprehensive error handling:

| Error Type | Handling |
|------------|----------|
| `FormatError` | Requery model with error template (up to `max_requeries` = 3) |
| `_BlockedActionError` | Requery with blocklist error template |
| `BashIncorrectSyntaxError` | Requery with syntax error template |
| `ContentPolicyViolationError` | Resample (just try again) |
| `_RetryWithOutput` | Requery with the output |
| `_RetryWithoutOutput` | Requery silently |
| `_ExitForfeit` | Autosubmit and exit |
| `CommandTimeoutError` | Autosubmit after consecutive timeouts |
| `ContextWindowExceededError` | Autosubmit and exit |
| `CostLimitExceededError` | Autosubmit and exit |
| `SwerexException` | Autosubmit and exit |
| Exhausted requeries | Autosubmit and exit |

**Key pattern:** On any terminal error, the agent attempts **autosubmission** — it runs `git add -A && git diff --cached > /root/model.patch` and submits whatever patch exists.

### Turn Management

Each turn produces a `StepOutput`:
```python
class StepOutput(BaseModel):
    query: list[dict]           # The messages sent to the model
    thought: str                # Parsed thought/discussion
    action: str                 # Parsed command to execute
    output: str                 # Raw model output
    observation: str            # Environment response
    execution_time: float
    done: bool
    exit_status: str | None
    submission: str | None      # The patch if submitted
    state: dict[str, str]       # Environment state (open_file, working_dir, diff)
    tool_calls: list[dict]      # Function calling tool calls
    tool_call_ids: list[str]
    thinking_blocks: list[dict] # Claude thinking blocks
```

### Observation Truncation

**File:** `sweagent/agent/agents.py` — `TemplateConfig`

```python
max_observation_length: int = 100_000  # characters
```

If an observation exceeds this limit, it's truncated with a `<response clipped>` message telling the agent how many characters were elided and suggesting alternative commands.

### Submission Detection

The submit tool writes a special marker `<<SWE_AGENT_SUBMISSION>>` to stdout. The `check_for_submission_cmd()` method detects this. The actual patch is read from `/root/model.patch`.

### Retry Agent

**File:** `sweagent/agent/agents.py` — `RetryAgent`

Wraps `DefaultAgent` to support multiple attempts:
- Runs `DefaultAgent` for each attempt
- After each submission, feeds it to a **reviewer** (separate LLM call)
- Decides whether to retry based on score/budget
- Resets environment between attempts (`env.hard_reset()`)
- Selects best attempt via `ScoreRetryLoop` or `ChooserRetryLoop`

### Shell Agent

**File:** `sweagent/agent/extra/shell_agent.py`

A collaborative agent where:
- The AI runs normally
- Pressing `^C` switches to human input mode
- Pressing `^D` switches back to AI mode
- The human must perform final submission

---

## 4. Environment/Runtime

### SWE-ReX Integration

**File:** `sweagent/environment/swe_env.py`

SWE-agent delegates all execution to **SWE-ReX** (`swe-rex` package), an external runtime abstraction:

```python
from swerex.deployment.abstract import AbstractDeployment
from swerex.deployment.config import DeploymentConfig, DockerDeploymentConfig, get_deployment
from swerex.runtime.abstract import BashAction, BashInterruptAction, CreateBashSessionRequest
```

Supported deployments:
- **Docker** (default): `DockerDeploymentConfig(image="python:3.11")`
- **Modal**: Cloud-based execution
- **Local**: Direct execution (no isolation)
- **Dummy**: For testing

### Environment Lifecycle

```
SWEEnv.start()
  → _init_deployment()          # Start Docker container
    → deployment.start()        # Async
    → Create bash session       # Sources /root/.bashrc, 10s timeout
    → Set env vars (LANG, LC_ALL, PIP_PROGRESS_BAR, PAGER)
  → reset()
    → cd /
    → _copy_repo()              # Clone/upload repository
    → _reset_repository()       # git checkout base_commit
    → Execute post_startup_commands
```

### Shell Customization

Environment variables set inside the container:
```python
env_variables = {
    "PAGER": "cat",       # Disable paging
    "MANPAGER": "cat",
    "LESS": "-R",
    "PIP_PROGRESS_BAR": "off",
    "TQDM_DISABLE": "1",
    "GIT_PAGER": "cat",
    "LANG": "C.UTF-8",
    "LC_ALL": "C.UTF-8",
}
```

The tool handler installs tools by:
1. Uploading each bundle to `/root/tools/<name>/`
2. Adding `/root/tools/<name>/bin/` to `$PATH`
3. Running `install.sh` if present
4. Verifying each command is available via `which`
5. Writing registry variables to `/root/.swe-agent-env`
6. Writing empty state to `/root/state.json`

### Command Execution

```python
def communicate(self, input: str, timeout: int = 25, *, check="ignore") -> str:
```

All commands run through `deployment.runtime.run_in_session(BashAction(...))`. Default timeout is 25 seconds per command, with a global `total_execution_timeout` of 1800 seconds (30 minutes). After 3 consecutive timeouts, the agent exits.

### Repository Handling

**File:** `sweagent/environment/repo.py`

Four repository source types:

| Type | Config | How It Works |
|------|--------|--------------|
| `GithubRepoConfig` | `github_url`, `base_commit` | Shallow clone (`git fetch --depth 1`) |
| `LocalRepoConfig` | `path`, `base_commit` | Upload entire directory to container |
| `PreExistingRepoConfig` | `repo_name`, `base_commit` | Already in container (SWE-bench images) |
| `SWESmithRepoConfig` | `repo_name`, `mirror_url`, `base_commit` | Fetch from GitHub mirror |

Reset sequence:
```bash
git fetch && git status && git restore . && git reset --hard && git checkout <base_commit> && git clean -fdq
```

---

## 5. LLM Providers

### LiteLLM Abstraction

**File:** `sweagent/agent/models.py`

SWE-agent uses **LiteLLM** as its universal LLM gateway. This means any model supported by LiteLLM works automatically:

```python
class LiteLLMModel(AbstractModel):
    def _single_query(self, messages, n=None, temperature=None):
        response = litellm.completion(
            model=self.config.name,
            messages=messages,
            temperature=self.config.temperature,
            top_p=self.config.top_p,
            api_key=self.config.choose_api_key(),
            fallbacks=self.config.fallbacks,
            **completion_kwargs,
        )
```

### Model Types

| Model Class | Config Class | Purpose |
|-------------|-------------|---------|
| `LiteLLMModel` | `GenericAPIModelConfig` | Any LiteLLM-supported model (GPT-4, Claude, Gemini, local, etc.) |
| `HumanModel` | `HumanModelConfig` | Human-in-the-loop via stdin |
| `HumanThoughtModel` | `HumanThoughtModelConfig` | Human provides both thought + action |
| `ReplayModel` | `ReplayModelConfig` | Replays actions from a `.traj` file |
| `InstantEmptySubmitTestModel` | `InstantEmptySubmitModelConfig` | Immediately submits empty patch (testing) |
| `PredeterminedTestModel` | N/A | Outputs predetermined sequences (unit tests) |

### Model Configuration

```python
class GenericAPIModelConfig(BaseModel):
    name: str                           # e.g., "gpt-4o", "claude-sonnet-4-20250514"
    per_instance_cost_limit: float = 3.0  # $ per task
    total_cost_limit: float = 0.0       # $ total across all tasks
    per_instance_call_limit: int = 0
    temperature: float = 0.0
    top_p: float | None = 1.0
    api_base: str | None = None
    api_key: SecretStr | None = None    # Supports ":::" for multiple keys
    stop: list[str] = []
    completion_kwargs: dict = {}        # Pass-through to litellm.completion
    convert_system_to_user: bool = False  # For models like o1
    retry: RetryConfig                  # 20 retries, 10-120s exponential backoff
    delay: float = 0.0                  # Minimum delay between queries
    fallbacks: list[dict] = []          # LiteLLM fallback models
    choose_api_key_by_thread: bool = True  # Thread-pinned keys for prompt caching
    max_input_tokens: int | None = None
    max_output_tokens: int | None = None
    custom_tokenizer: dict | None = None
```

### API Key Management

Multiple API keys can be provided, separated by `:::`. When `choose_api_key_by_thread=True`, each thread consistently uses the same key (important for prompt caching in batch runs). Keys starting with `$` are interpreted as environment variable names.

### Special Claude Handling

```python
# Claude 3.7/Sonnet 4 default to 64k output unless 128k beta header is set
is_claude_3_7 = "claude-3-7-sonnet" in self.config.name or "claude-sonnet-4" in self.config.name
if is_claude_3_7 and not has_128k_beta_header:
    self.model_max_output_tokens = 64000
```

### Cost Tracking

```python
class InstanceStats(BaseModel):
    instance_cost: float = 0
    tokens_sent: int = 0
    tokens_received: int = 0
    api_calls: int = 0

class GlobalStats(BaseModel):
    total_cost: float = 0
    last_query_timestamp: float = 0
```

Global stats are protected by a threading `Lock` for concurrent batch execution. Cost is computed via `litellm.cost_calculator.completion_cost()`.

---

## 6. Context/Token Management

### History Processors

**File:** `sweagent/agent/history_processors.py`

History processors are a pipeline that transforms the message history before each model query:

```python
messages = filtered_history
for processor in self.history_processors:
    messages = processor(messages)
```

| Processor | Type Key | Purpose |
|-----------|----------|---------|
| `DefaultHistoryProcessor` | `default` | Pass-through (no-op) |
| `LastNObservations` | `last_n_observations` | Keep only last N observations; older ones become "({n} lines omitted)". The classic SWE-agent 0.7 approach. |
| `ClosedWindowHistoryProcessor` | `closed_window` | Replace outdated file windows with summaries. Only keeps the last window for each file. |
| `CacheControlHistoryProcessor` | `cache_control` | Add Anthropic `cache_control` markers to last N user messages. Critical for prompt caching. |
| `TagToolCallObservations` | `tag_tool_call_observations` | Tag specific tool outputs for keep/remove decisions. |
| `RemoveRegex` | `remove_regex` | Strip patterns from history (e.g., `<diff>.*</diff>`). |
| `ImageParsingHistoryProcessor` | `image_parsing` | Parse base64 images from markdown into multi-modal format. |

### Prompt Caching Strategy

The default config uses:
```yaml
history_processors:
  - type: cache_control
    last_n_messages: 2
```

This adds `cache_control: {"type": "ephemeral"}` to the last 2 user/tool messages, enabling Anthropic's prompt caching for multi-turn conversations. The `polling` parameter on `LastNObservations` can batch window shifts to avoid cache invalidation.

### Context Window Protection

1. **Pre-query check:** Token count computed via `litellm.utils.token_counter()` before sending. If it exceeds `model_max_input_tokens`, a `ContextWindowExceededError` is raised.
2. **Post-observation truncation:** Observations exceeding `max_observation_length` (default 100,000 chars) are clipped.
3. **History elision:** `LastNObservations` summarizes old outputs as line counts.
4. **Graceful exit:** On `ContextWindowExceededError`, the agent attempts autosubmission rather than crashing.

---

## 7. Configuration

### YAML Config System

Configuration is Pydantic-based with YAML file loading. The CLI supports both YAML files and dot-notation overrides:

```bash
sweagent run \
    --config config/default.yaml \
    --agent.model.name "claude-sonnet-4-20250514" \
    --agent.model.per_instance_cost_limit 5.0 \
    --env.repo.github_url https://github.com/org/repo \
    --problem_statement.github_url https://github.com/org/repo/issues/42
```

### Config Hierarchy

```
RunSingleConfig
├── env: EnvironmentConfig
│   ├── deployment: DeploymentConfig (Docker/Modal/Local)
│   ├── repo: RepoConfig (GitHub/Local/PreExisting)
│   ├── post_startup_commands: list[str]
│   └── post_startup_command_timeout: int = 500
├── agent: AgentConfig (DefaultAgentConfig | RetryAgentConfig | ShellAgentConfig)
│   ├── templates: TemplateConfig
│   │   ├── system_template: str
│   │   ├── instance_template: str
│   │   ├── next_step_template: str
│   │   ├── next_step_truncated_observation_template: str
│   │   ├── max_observation_length: int = 100_000
│   │   ├── demonstrations: list[Path]
│   │   └── ...
│   ├── tools: ToolConfig
│   │   ├── bundles: list[Bundle]
│   │   ├── parse_function: ParseFunction
│   │   ├── filter: ToolFilterConfig
│   │   ├── env_variables: dict
│   │   ├── registry_variables: dict
│   │   ├── submit_command: str = "submit"
│   │   ├── execution_timeout: int = 30
│   │   └── total_execution_timeout: int = 1800
│   ├── model: ModelConfig
│   ├── history_processors: list[HistoryProcessor]
│   ├── max_requeries: int = 3
│   └── action_sampler: ActionSamplerConfig | None
├── problem_statement: ProblemStatementConfig
├── output_dir: Path
├── actions: RunSingleActionConfig (open_pr, apply_patch_locally)
└── env_var_path: Path | None
```

### Template Variables

Templates use Jinja2 syntax (`{{variable}}`). Available variables:

| Variable | Source |
|----------|--------|
| `problem_statement` | Problem statement text |
| `command_docs` | Auto-generated tool documentation |
| `repo` | Repository name |
| `observation` | Last command output |
| `open_file` | Currently open file (from state) |
| `working_dir` | Current directory (from state) |
| `diff` | Git diff (from state, if diff_state bundle loaded) |
| `WINDOW` | Window size (from env_variables) |
| Any `env_variables` | Passed through to templates |
| Any `extra_fields` | From problem statement |

### Default Config (`config/default.yaml`)

The current default uses Anthropic-style tools with function calling:
```yaml
agent:
  tools:
    bundles:
      - path: tools/registry
      - path: tools/edit_anthropic      # str_replace_editor
      - path: tools/review_on_submit_m  # submit with self-review
    registry_variables:
      USE_FILEMAP: 'true'
      SUBMIT_REVIEW_MESSAGES: [...]     # Review checklist before final submit
    enable_bash_tool: true
    parse_function:
      type: function_calling           # OpenAI function calling format
  history_processors:
    - type: cache_control
      last_n_messages: 2
```

### SWE-agent 0.7 Config (`config/sweagent_0_7/07.yaml`)

The original approach:
```yaml
agent:
  tools:
    bundles:
      - path: tools/windowed             # open, goto, scroll
      - path: tools/search              # find_file, search_dir, search_file
      - path: tools/windowed_edit_linting # edit with line range + linting
      - path: tools/submit
    env_variables:
      WINDOW: 100
      OVERLAP: 2
    parse_function:
      type: thought_action              # DISCUSSION + ```code``` format
  history_processors:
    - type: last_n_observations
      n: 5
  templates:
    demonstrations:
      - trajectories/demonstrations/replay__marshmallow-code__marshmallow-1867__...traj
```

---

## 8. SWE-bench Integration

### Batch Execution

**File:** `sweagent/run/run_batch.py`

```bash
sweagent run-batch \
    --instances.type swe_bench \
    --instances.subset lite \
    --instances.split dev \
    --instances.slice :50 \
    --instances.shuffle=True \
    --config config/default.yaml \
    --agent.model.name gpt-4o \
    --num_workers 4
```

### Instance Loading

**File:** `sweagent/run/batch_instances.py`

Supports multiple instance sources:

| Source Type | Config | Description |
|-------------|--------|-------------|
| `swe_bench` | `SWEBenchInstances` | Load from HuggingFace SWE-bench dataset |
| `file` | `FileInstances` | Load from JSON/JSONL file |
| `huggingface` | `HuggingFaceInstances` | Generic HuggingFace dataset |

Each instance (`BatchInstance`) contains:
- `env: EnvironmentConfig` — Docker image, repo config, post-startup commands
- `problem_statement: ProblemStatementConfig` — Issue text

For SWE-bench, each instance uses a pre-built Docker image with dependencies installed, and the repository is already present at a specific commit.

### Parallel Execution

`RunBatch` uses `ThreadPoolExecutor` for parallel execution:
- Random startup delays (`random_delay_multiplier * num_workers`) to avoid thundering herd
- Thread-pinned API keys for prompt caching
- Per-instance log files (trace, debug, info levels)
- Skip already-completed instances (`redo_existing=False`)
- Progress tracking with Rich live display

### Evaluation

**File:** `sweagent/run/hooks/swe_bench_evaluate.py`

After batch completion, calls `sb-cli submit` to evaluate against SWE-bench:
```python
args = ["sb-cli", "submit", subset, split,
        "--predictions_path", preds_path,
        "--run_id", run_id,
        "--output_dir", output_dir]
```

Supports continuous submission during the run (every 30 seconds).

### Prediction Format

After each instance, predictions are saved as:
```json
{
    "instance_id": "...",
    "model_patch": "... git diff ...",
    "model_name_or_path": "..."
}
```

These are merged into a single `preds.json` for SWE-bench evaluation.

---

## 9. Permissions/Safety

### Command Blocking

**File:** `sweagent/tools/tools.py` — `ToolFilterConfig`

Three levels of command filtering:

1. **Prefix blocklist:** Commands starting with `vim`, `vi`, `emacs`, `nano`, `nohup`, `gdb`, `less`, `tail -f`, `python -m venv`, `make`
2. **Exact match blocklist:** `python`, `python3`, `ipython`, `bash`, `sh`, `/bin/bash`, `/bin/sh`, `su`
3. **Regex-gated:** `radare2` and `r2` only allowed with `-c` flag (non-interactive mode)

### Cost Controls

- **Per-instance cost limit:** Default $3.00. Configurable via `per_instance_cost_limit`.
- **Total cost limit:** Across all instances. Default $0 (unlimited).
- **Per-instance call limit:** Maximum API calls per task. Default 0 (unlimited).
- On exceeding limits: `CostLimitExceededError` triggers autosubmission.

### Execution Timeouts

- **Per-command timeout:** 30 seconds (configurable)
- **Total execution timeout:** 1800 seconds (30 minutes)
- **Consecutive timeout limit:** 3 consecutive timeouts kill the agent
- **Install timeout:** 300 seconds for tool installation commands

### Container Isolation

All agent code runs inside Docker containers via SWE-ReX. The agent cannot access the host filesystem. Repository changes are captured as git diffs.

### Bash Syntax Checking

SWE-ReX performs `bash -n` syntax checking before executing commands. If the command has syntax errors, a `BashIncorrectSyntaxError` is raised and the agent is asked to correct it (up to `max_requeries` times).

### Multiline Command Guards

**File:** `sweagent/tools/utils.py`

Multi-line commands (like `edit`) use heredoc syntax (`<< 'EOF'`) to prevent injection. The `guard_multiline_input()` function automatically wraps multi-line arguments in heredocs.

---

## 10. Unique Features

### 10 Output Parsing Formats

**File:** `sweagent/tools/parsing.py`

SWE-agent supports an unusually wide range of model output formats:

| Parser | Type Key | Format |
|--------|----------|--------|
| `FunctionCallingParser` | `function_calling` | LiteLLM tool calls (default) |
| `ThoughtActionParser` | `thought_action` | DISCUSSION + \`\`\`code\`\`\` |
| `XMLThoughtActionParser` | `xml_thought_action` | Text + `<command>...</command>` |
| `XMLFunctionCallingParser` | `xml_function_calling` | Text + `<function=name>...<parameter=arg>...</parameter></function>` |
| `JsonParser` | `json` | JSON `{"thought": "...", "command": {"name": "...", "arguments": {...}}}` |
| `ActionParser` | `action` | Single command line |
| `ActionOnlyParser` | `action_only` | Raw command (human mode) |
| `EditFormat` | `edit_format` | Text + \`\`\`replacement\`\`\` |
| `BashCodeBlockParser` | `all_bash_code_blocks` | All \`\`\`bash blocks |
| `SingleBashCodeBlockParser` | `single_bash_code_block` | Exactly one \`\`\`bash block |
| `Identity` | `identity` | Pass-through (thought = action = raw output) |

### Action Samplers (Best-of-N)

**File:** `sweagent/agent/action_sampler.py`

Two strategies for selecting from multiple sampled actions:

**AskColleagues:** Sample N completions, format them as "colleague suggestions", then ask the model to pick the best one:
```
Your colleagues had the following ideas:
Thought (colleague 0): ...
Proposed Action (colleague 0): ...
...
Please summarize and compare and choose one action.
```

**BinaryTrajectoryComparison:** Tournament-style pairwise comparison:
1. Sample 4-10 completions
2. Filter duplicates and unparseable outputs
3. If edits are proposed, sample more (up to `max_n_samples`)
4. Run pairwise "which is better?" queries
5. Winner advances (like bracket tournament)

### Reviewer / Retry Loop

**File:** `sweagent/agent/reviewer.py`

Two retry strategies:

**ScoreRetryLoop:** After each attempt, a reviewer LLM scores the submission (1-10). If score < `accept_score`, retry. Supports:
- Multiple review samples with std reduction
- Failure penalty for non-submitted attempts
- Trajectory formatting with action/output filters
- Budget-aware (won't retry if insufficient budget remains)

**ChooserRetryLoop:** Run N attempts, then a chooser LLM picks the best:
- Optional preselector to narrow candidates
- Filters to only consider "submitted" attempts
- Pairwise comparison of formatted submissions

### Shell Agent (Human-AI Collaboration)

**File:** `sweagent/agent/extra/shell_agent.py`

A unique interactive mode where:
- AI agent runs autonomously
- Press `^C` to interrupt and take over (human mode)
- Type commands manually
- Press `^D` to hand back to AI
- AI must complete, but human must submit

### Demonstration System

Demonstrations (few-shot examples) are pre-recorded trajectories stored as `.traj` JSON files. They can be:
- Injected as a single message (`demonstration_template`)
- Replayed step-by-step into history (`put_demos_in_history=True`)
- Created from successful runs via `sweagent traj-to-demo`
- Replayed to fill in environment outputs via `sweagent run-replay`

### Windowed File Interface

The `WindowedFile` class (`tools/windowed/lib/windowed_file.py`) is a rich abstraction:
- Maintains a scrollable window (default 100 lines, 2-line overlap)
- Line numbers, status lines, pre/post context
- Search-replace within window or entire file
- Undo edit support
- State persisted via the registry system

### State Commands

Each tool bundle can define a `state_command` that runs after every action. This populates `/root/state.json` with environment state that gets injected into prompt templates:
- `windowed/`: Tracks `open_file` and `working_dir`
- `diff_state/`: Tracks current git diff
- `edit_anthropic/`: Tracks open file and working dir

### Hook System

Three independent hook systems for extensibility:

| Hook Type | Base Class | Lifecycle Points |
|-----------|-----------|------------------|
| Agent hooks | `AbstractAgentHook` | on_init, on_run_start, on_step_start, on_actions_generated, on_action_started, on_action_executed, on_step_done, on_run_done, on_setup_attempt, on_model_query, on_query_message_added, on_setup_done, on_tools_installation_started |
| Environment hooks | `EnvHook` | on_init, on_copy_repo_started, on_environment_startup, on_start_deployment, on_close |
| Run hooks | `RunHook` | on_init, on_start, on_end, on_instance_start, on_instance_completed |

All use the `CombinedXxxHook` pattern (composite) to fan-out to multiple hooks.

### Trajectory Inspector

Two inspection tools:
- `sweagent inspect <file.traj>` — Terminal-based viewer (Textual TUI)
- `sweagent inspector` — Web-based viewer (Flask)

### SWE-bench Multimodal Support

`SWEBenchMultimodalProblemStatement` downloads issue images, converts them to base64, and injects them as `![](data:image/png;base64,...)` markdown. The `ImageParsingHistoryProcessor` then converts these into multi-modal message format for models that support vision.

---

## Architecture Summary

| Aspect | Approach |
|--------|----------|
| **Language** | Python 3.11+, Pydantic v2, async via `asyncio.run()` |
| **LLM abstraction** | LiteLLM (100+ providers via single interface) |
| **Execution** | SWE-ReX (Docker, Modal, local) |
| **Config** | Pydantic models + YAML files + CLI dot-notation |
| **Tool system** | Bash scripts in bundles, installed to container PATH |
| **Parsing** | 10+ output format parsers |
| **Context management** | History processor pipeline (elide, cache, regex remove) |
| **Safety** | Command blocklist, cost limits, execution timeouts, container isolation |
| **Retry** | Score-based or chooser-based retry loops with separate reviewer model |
| **Codebase size** | ~5,000 lines core Python + ~2,000 lines tool scripts |
| **Testing** | Pytest with markers (slow, ctf) |

### Key Architectural Decisions

1. **Tools as bash scripts, not Python functions.** Tools run inside the container as shell commands, making them language-agnostic and isolating them from the agent runtime.

2. **Separation of parsing from execution.** The parser extracts (thought, action) from model output; the action is then executed as a bash command. This allows the same tools to work with function calling, XML, backtick-wrapped, or JSON output formats.

3. **State via filesystem, not memory.** The registry (`/root/.swe-agent-env`) and state file (`/root/state.json`) persist across tool invocations using the filesystem rather than environment variables or in-memory state.

4. **Template-driven prompts.** Every message (system, instance, next_step, error, truncation) is a Jinja2 template. This allows complete prompt customization via YAML config without touching code.

5. **Autosubmission on error.** Rather than losing work on any error, the agent always attempts to extract and submit whatever patch exists. This maximizes benchmark scores on SWE-bench where partial fixes still count.
