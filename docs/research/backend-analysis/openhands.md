# OpenHands Backend Architecture Analysis

> Python cloud agent platform (formerly OpenDevin) -- ~65k GitHub stars.
> Codebase snapshot analyzed: March 2026, mid-migration from V0 to V1.

---

## 1. Project Structure

```
openhands/                     # Root
├── openhands/                 # Main Python package
│   ├── agenthub/              # Agent implementations (CodeAct, Browsing, ReadOnly, etc.)
│   ├── app_server/            # NEW V1 application server (replacing server/)
│   ├── controller/            # AgentController, state machine, stuck detection
│   ├── core/                  # Config, schema, exceptions, main loop, message format
│   ├── critic/                # Agent output critique/evaluation
│   ├── events/                # Event system (actions, observations, serialization, stream)
│   ├── experiments/           # Experiment tracking
│   ├── integrations/          # Git provider integrations (GitHub, GitLab, etc.)
│   ├── io/                    # I/O utilities, JSON handling
│   ├── linter/                # Code linting
│   ├── llm/                   # LLM abstraction (litellm wrapper, metrics, routing)
│   ├── mcp/                   # MCP protocol client
│   ├── memory/                # Memory system: condenser, conversation memory, microagent retrieval
│   ├── microagent/            # Microagent loading, types, frontmatter parsing
│   ├── resolver/              # Issue resolver (SWE-bench pipeline)
│   ├── runtime/               # Sandbox runtimes (Docker, Remote, Local, K8s, CLI)
│   ├── security/              # Security analyzers (Invariant, GraySwan, LLM-based)
│   ├── server/                # V0 web server (FastAPI + Socket.IO)
│   ├── storage/               # File store abstraction
│   └── utils/                 # Async utils, prompt manager, shutdown listener
├── skills/                    # Global microagent markdown files (~27 skills)
├── containers/                # Docker build contexts
├── frontend/                  # React web UI
├── enterprise/                # Enterprise features
├── tests/                     # Test suite
├── third_party/               # Vendored dependencies
├── scripts/                   # Build/deploy scripts
└── pyproject.toml             # Poetry/uv project config
```

### Dual-Stack Note (V0 / V1 Migration)

Nearly every file in the V0 codebase carries a deprecation header:

```python
# IMPORTANT: LEGACY V0 CODE - Deprecated since version 1.0.0, scheduled for removal April 1, 2026
# V1 agentic core (SDK): https://github.com/OpenHands/software-agent-sdk
# V1 application server (in this repo): openhands/app_server/
```

V1 extracts the agent core into a separate `software-agent-sdk` repo and replaces the web server with `openhands/app_server/`. The analysis below covers the V0 codebase that is still the active production code path.

---

## 2. Tools / Actions

OpenHands uses a typed Action/Observation event system rather than a tool registry. Each "tool" is a `ChatCompletionToolParam` dict (litellm format) defined in `openhands/agenthub/codeact_agent/tools/`.

### CodeAct Agent Tools (Primary Agent)

| Tool Name | File | Action Class | Description |
|-----------|------|--------------|-------------|
| `execute_bash` | `tools/bash.py` | `CmdRunAction` | Persistent bash shell. Supports `is_input` for stdin, `timeout` for hard timeouts, `C-c`/`C-d` control signals. |
| `str_replace_editor` | `tools/str_replace_editor.py` | `FileReadAction` / `FileEditAction` | SWE-bench-style editor with `view`, `create`, `str_replace`, `insert`, `undo_edit` commands. Uses `openhands-aci` library. |
| `edit_file` | `tools/llm_based_edit.py` | `FileEditAction` | LLM-based file editing with line ranges and draft content (deprecated in favor of str_replace_editor). |
| `execute_ipython_cell` | `tools/ipython.py` | `IPythonRunCellAction` | Jupyter IPython cell execution within the sandbox. Supports magic commands. |
| `browser` | `tools/browser.py` | `BrowseInteractiveAction` | BrowserGym-powered browser interaction with 15 high-level actions (goto, click, fill, scroll, etc.). |
| `think` | `tools/think.py` | `AgentThinkAction` | Scratchpad for reasoning. No side effects. |
| `finish` | `tools/finish.py` | `AgentFinishAction` | Signals task completion with a summary message. |
| `request_condensation` | `tools/condensation_request.py` | `CondensationRequestAction` | Agent-initiated request to condense conversation history. |
| `task_tracker` | `tools/task_tracker.py` | `TaskTrackingAction` | Structured task management with `view`/`plan` commands and todo/in_progress/done statuses. |
| MCP tools | (dynamic) | `MCPAction` | Any tool registered via MCP servers (stdio/SSE). Dynamically added to tool list. |

### ReadOnly Agent Tools

| Tool Name | File | Description |
|-----------|------|-------------|
| `grep` | `readonly_agent/tools/grep.py` | Search file contents (read-only grep) |
| `glob` | `readonly_agent/tools/glob.py` | Find files by glob pattern |
| `view` | `readonly_agent/tools/view.py` | View file contents (read-only) |
| `think` | (shared) | Reasoning scratchpad |
| `finish` | (shared) | Task completion |

### Security Risk Annotation

Every tool that modifies the environment requires a `security_risk` parameter with values `["low", "medium", "high"]`. This is used by the security analyzer system for confirmation gating.

```python
# From tools/security_utils.py
RISK_LEVELS = ['low', 'medium', 'high']
SECURITY_RISK_DESC = 'The security risk of the action. ...'
```

---

## 3. Agent Loop

### Core Loop Architecture

The agent loop is **event-driven**, not a traditional while-loop. It is orchestrated by the `AgentController` class.

**File:** `openhands/controller/agent_controller.py`

```
EventStream emits event
    -> AgentController.on_event() callback
        -> Decides if agent should step (should_step())
        -> Calls agent.step(state) -> returns Action
        -> Action is added to EventStream
        -> Runtime subscribes to EventStream, executes action
        -> Runtime adds Observation to EventStream
        -> Loop repeats
```

### AgentController Key Responsibilities

1. **State machine management** -- transitions between `AgentState` values (LOADING, RUNNING, AWAITING_USER_INPUT, PAUSED, STOPPED, FINISHED, ERROR, etc.)
2. **Stuck detection** -- `StuckDetector` checks for 5 loop scenarios (repeated action/obs, repeated errors, monologue, alternating patterns, context window error loops)
3. **Delegation** -- starts/ends child `AgentController` instances for sub-agents
4. **Iteration/budget limits** -- `IterationControlFlag` and `BudgetControlFlag` track global steps and USD spend
5. **Security analysis** -- optional `SecurityAnalyzer` can gate actions requiring confirmation
6. **Replay** -- `ReplayManager` can replay past events for resumption

### The Step Method

```python
# AgentController._step() simplified:
async def _step(self):
    if self.get_agent_state() != AgentState.RUNNING:
        return
    if self._pending_action:
        return  # waiting for observation

    # Check stuck, budget, iteration limits
    if self._is_stuck():
        await self._react_to_exception(AgentStuckInLoopError())
        return

    # Call the agent
    action = self.agent.step(self.state)

    # Post-process: handle security, confirmation mode
    await self._handle_security_analyzer(action)

    # Add action to event stream
    self.event_stream.add_event(action, EventSource.AGENT)
    self._pending_action = action
```

### Agent.step() (CodeActAgent)

```python
def step(self, state: State) -> Action:
    # 1. Drain pending actions queue
    if self.pending_actions:
        return self.pending_actions.popleft()

    # 2. Condense history (may return Condensation instead of View)
    match self.condenser.condensed_history(state):
        case View(events=events):
            condensed_history = events
        case Condensation(action=condensation_action):
            return condensation_action  # controller re-steps immediately

    # 3. Build messages from condensed history
    messages = self._get_messages(condensed_history, ...)

    # 4. Call LLM
    response = self.llm.completion(messages=messages, tools=self.tools)

    # 5. Parse response into Actions
    actions = self.response_to_actions(response)
    for action in actions:
        self.pending_actions.append(action)
    return self.pending_actions.popleft()
```

### State Machine

```python
class AgentState(str, Enum):
    LOADING = 'loading'
    RUNNING = 'running'
    AWAITING_USER_INPUT = 'awaiting_user_input'
    PAUSED = 'paused'
    STOPPED = 'stopped'
    FINISHED = 'finished'
    REJECTED = 'rejected'
    ERROR = 'error'
    AWAITING_USER_CONFIRMATION = 'awaiting_user_confirmation'
    USER_CONFIRMED = 'user_confirmed'
    USER_REJECTED = 'user_rejected'
    RATE_LIMITED = 'rate_limited'
```

---

## 4. Docker Sandboxing

This is OpenHands' signature feature. All code execution happens inside a Docker container, not on the host.

### Architecture

```
Host Machine
├── AgentController (Python process)
├── EventStream (file-backed, threaded)
└── DockerRuntime
    ├── Builds/pulls sandbox image
    ├── Starts container with port mapping
    └── Communicates via HTTP to:
        └── Docker Container
            ├── action_execution_server.py (FastAPI, runs INSIDE container)
            ├── BashSession (persistent shell)
            ├── JupyterPlugin (IPython kernel)
            ├── BrowserEnv (Playwright/BrowserGym)
            ├── OHEditor (openhands-aci file editor)
            ├── MCPProxyManager
            └── VSCodePlugin (optional)
```

### Key Files

| File | Purpose |
|------|---------|
| `runtime/base.py` | `Runtime` abstract base class -- defines the interface |
| `runtime/impl/docker/docker_runtime.py` | `DockerRuntime` -- builds image, starts container, communicates via HTTP |
| `runtime/action_execution_server.py` | FastAPI server that runs **inside** the container, receives actions, returns observations |
| `runtime/impl/action_execution/action_execution_client.py` | HTTP client on the host side that sends actions to the container |
| `runtime/utils/bash.py` | `BashSession` -- persistent bash shell with soft timeout and stdin support |
| `runtime/builder/` | Docker image build pipeline |

### How It Works

1. `DockerRuntime.__init__()` builds or pulls the sandbox Docker image
2. Starts a container with the `action_execution_server.py` as the entrypoint
3. Maps ports for the execution server (30000-39999), VSCode (40000-49999), and app ports (50000-59999)
4. The host-side `ActionExecutionClient` sends HTTP POST requests with serialized `Action` objects
5. The container-side FastAPI server deserializes, executes the action, and returns a serialized `Observation`
6. Communication is secured with `SESSION_API_KEY` in the `X-Session-API-Key` header
7. Secrets are masked from the event stream via `EventStream._replace_secrets()`

### Runtime Implementations

| Runtime | File | Description |
|---------|------|-------------|
| `DockerRuntime` | `impl/docker/docker_runtime.py` | Local Docker container. Primary runtime. |
| `RemoteRuntime` | `impl/remote/remote_runtime.py` | Remote execution server (cloud deployment). |
| `LocalRuntime` | `impl/local/local_runtime.py` | Local execution (no Docker, for development). |
| `KubernetesRuntime` | `impl/action_execution/kubernetes_runtime.py` | Kubernetes pod-based execution. |
| `CLIRuntime` | `impl/action_execution/cli_runtime.py` | CLI-based execution. |

### Runtime Plugins

Plugins are initialized inside the container at startup:

| Plugin | Purpose |
|--------|---------|
| `AgentSkillsRequirement` | Pre-loads Python helper functions into the IPython environment |
| `JupyterRequirement` | Starts a Jupyter kernel inside the container |
| `VSCodeRequirement` | Starts a VS Code server (non-headless mode only) |

---

## 5. Event System

### Core Design

OpenHands uses an **event-sourced** architecture. All state is derived from an append-only event stream. Events are either **Actions** (agent/user intent) or **Observations** (environment responses).

**File:** `openhands/events/stream.py`

### Event Base Class

```python
@dataclass
class Event:
    INVALID_ID = -1
    # Properties: id, timestamp, source, cause, timeout, llm_metrics, tool_call_metadata, response_id
```

### Event Sources

```python
class EventSource(str, Enum):
    AGENT = 'agent'
    USER = 'user'
    ENVIRONMENT = 'environment'
```

### EventStream

The `EventStream` class is the central nervous system:

- **Thread-safe** -- uses `threading.Lock` for ID assignment, `queue.Queue` for async dispatch
- **Persistent** -- writes each event as JSON to a `FileStore` (local filesystem or cloud)
- **Paginated caching** -- groups events into pages for efficient reads
- **Secret masking** -- automatically replaces secrets in serialized event data
- **Subscriber model** -- subscribers register with `subscribe(subscriber_id, callback, callback_id)`

```python
class EventStreamSubscriber(str, Enum):
    AGENT_CONTROLLER = 'agent_controller'
    RESOLVER = 'openhands_resolver'
    SERVER = 'server'
    RUNTIME = 'runtime'
    MEMORY = 'memory'
    MAIN = 'main'
    TEST = 'test'
```

### Actions (Agent Intent)

| Action | Description |
|--------|-------------|
| `CmdRunAction` | Execute bash command |
| `IPythonRunCellAction` | Execute IPython cell |
| `FileReadAction` | Read a file |
| `FileWriteAction` | Write/overwrite a file |
| `FileEditAction` | Edit a file (str_replace or LLM-based) |
| `BrowseURLAction` | Navigate to URL |
| `BrowseInteractiveAction` | Browser interaction (click, fill, etc.) |
| `MessageAction` | User or agent message |
| `SystemMessageAction` | System prompt message |
| `AgentFinishAction` | Agent declares task complete |
| `AgentRejectAction` | Agent rejects the task |
| `AgentDelegateAction` | Delegate to a sub-agent |
| `AgentThinkAction` | Agent reasoning (no side effect) |
| `ChangeAgentStateAction` | State transition request |
| `RecallAction` | Trigger microagent/knowledge retrieval |
| `MCPAction` | Call an MCP tool |
| `TaskTrackingAction` | Update task list |
| `CondensationAction` | Apply condensation to history |
| `CondensationRequestAction` | Request history condensation |
| `LoopRecoveryAction` | Recover from stuck loop |
| `NullAction` | No-op |

### Observations (Environment Response)

| Observation | Description |
|-------------|-------------|
| `CmdOutputObservation` | Bash command output + exit code |
| `IPythonRunCellObservation` | IPython cell output |
| `FileReadObservation` | File contents |
| `FileWriteObservation` | Write confirmation |
| `FileEditObservation` | Edit result (diff) |
| `BrowserOutputObservation` | Browser page state (axtree, screenshot, URL) |
| `ErrorObservation` | Error message |
| `AgentStateChangedObservation` | State transition notification |
| `AgentDelegateObservation` | Delegate completion result |
| `AgentThinkObservation` | Think acknowledgment |
| `AgentCondensationObservation` | Condensation summary |
| `RecallObservation` | Retrieved microagent knowledge |
| `MCPObservation` | MCP tool result |
| `TaskTrackingObservation` | Task list state |
| `LoopDetectionObservation` | Loop recovery options |
| `SuccessObservation` | Generic success |
| `UserRejectObservation` | User rejected action |
| `FileDownloadObservation` | Downloaded file info |
| `NullObservation` | No-op |

### Event Flow

```
User sends message
    -> EventStream.add_event(MessageAction, source=USER)
    -> AgentController.on_event() fires
        -> Creates RecallAction for microagent retrieval
        -> EventStream.add_event(RecallAction, source=USER)
        -> Memory.on_event() fires
            -> Finds matching microagents
            -> EventStream.add_event(RecallObservation, source=ENVIRONMENT)
        -> AgentController.on_event() fires again (NullObservation from RecallAction)
        -> agent.step(state) called
        -> Returns CmdRunAction
        -> EventStream.add_event(CmdRunAction, source=AGENT)
        -> Runtime.on_event() fires
            -> Sends action to container
            -> Gets observation back
            -> EventStream.add_event(CmdOutputObservation, source=ENVIRONMENT)
        -> AgentController.on_event() fires
        -> agent.step(state) called again
        -> ... cycle continues
```

---

## 6. LLM Providers

### litellm as the Universal Adapter

OpenHands delegates all LLM communication to **litellm**, using it as a universal provider adapter. There is no per-provider implementation -- litellm handles the protocol differences.

**File:** `openhands/llm/llm.py`

```python
class LLM(RetryMixin, DebugMixin):
    def __init__(self, config: LLMConfig, service_id: str, ...):
        self._completion = partial(
            litellm_completion,
            model=self.config.model,
            api_key=self.config.api_key,
            base_url=self.config.base_url,
            ...
        )
```

### Supported Providers (via litellm)

All providers that litellm supports work out of the box. The codebase has explicit handling for:

| Provider | Model Prefix | Special Handling |
|----------|-------------|------------------|
| Anthropic (Claude) | `claude-*` | Prompt caching, extended thinking config, top_p constraints |
| OpenAI | `gpt-*`, `o1-*`, `o3-*`, `o4-*` | Short tool descriptions for older models |
| Google Gemini | `gemini-*` | Safety settings, thinking budget mapping |
| Azure OpenAI | `azure/*` | `max_tokens` instead of `max_completion_tokens` |
| AWS Bedrock | `bedrock/*` | AWS credential passthrough |
| OpenRouter | `openrouter/*` | Model info lookup |
| Ollama | `ollama/*` | Local model detection, top_p adjustment |
| Hugging Face | `huggingface/*` | top_p default override |
| Mistral | `mistral/*` | Safety settings |
| LiteLLM Proxy | `litellm_proxy/*` | Model info from proxy API |
| OpenHands Proxy | `openhands/*` | Rewritten to `litellm_proxy/*` with managed base URL |
| Custom/Local | Any with `base_url` | localhost detection |

### LLMRegistry

**File:** `openhands/llm/llm_registry.py`

The `LLMRegistry` manages LLM instances as named services:

```python
class LLMRegistry:
    def __init__(self, config: OpenHandsConfig):
        self.service_to_llm: dict[str, LLM] = {}
        self.active_agent_llm = self.get_llm('agent', llm_config)

    def get_llm(self, service_id: str, config: LLMConfig) -> LLM: ...
    def get_router(self, agent_config: AgentConfig) -> LLM: ...
```

### Model Routing (RouterLLM)

**File:** `openhands/llm/router/base.py`

`RouterLLM` inherits from `LLM` and supports routing requests to different models based on rules:

```python
class RouterLLM(LLM):
    """Base class for multiple LLM acting as a unified LLM."""
    # primary_llm + llms_for_routing
    # _select_llm() determines which LLM handles each request
```

A `rule_based` router implementation exists in `openhands/llm/router/rule_based/`.

### Function Calling Mock

For models that do not support native function calling, OpenHands converts tool-call messages to text prompts and parses the response back:

```python
# In LLM.wrapper():
if mock_function_calling and 'tools' in kwargs:
    messages = convert_fncall_messages_to_non_fncall_messages(messages, tools)
    # ... after getting response ...
    resp = convert_non_fncall_messages_to_fncall_messages(messages + [response], tools)
```

### Model Feature Detection

**File:** `openhands/llm/model_features.py`

Uses glob patterns to determine model capabilities:

```python
@dataclass(frozen=True)
class ModelFeatures:
    supports_function_calling: bool
    supports_reasoning_effort: bool
    supports_prompt_cache: bool
    supports_stop_words: bool
```

---

## 7. Context / Token Management

### Condenser System

OpenHands' context management uses a **condenser** pattern -- pluggable strategies that reduce event history before sending it to the LLM.

**File:** `openhands/memory/condenser/condenser.py`

```python
class Condenser(ABC):
    @abstractmethod
    def condense(self, view: View) -> View | Condensation: ...
```

Two return types:
- `View` -- a filtered list of events to use as context
- `Condensation` -- an instruction to modify the event stream (agent must return this as its action, then re-step)

### Condenser Implementations

| Condenser | File | Strategy |
|-----------|------|----------|
| `NoOpCondenser` | `impl/no_op_condenser.py` | Pass through all events unchanged |
| `RecentEventsCondenser` | `impl/recent_events_condenser.py` | Keep only the N most recent events |
| `ConversationWindowCondenser` | `impl/conversation_window_condenser.py` | Sliding window over events |
| `ObservationMaskingCondenser` | `impl/observation_masking_condenser.py` | Mask/truncate large observations |
| `LLMAttentionCondenser` | `impl/llm_attention_condenser.py` | Use LLM to score event importance |
| `LLMSummarizingCondenser` | `impl/llm_summarizing_condenser.py` | LLM-generated summary of forgotten events |
| `StructuredSummaryCondenser` | `impl/structured_summary_condenser.py` | Structured summary with categories |
| `AmortizedForgettingCondenser` | `impl/amortized_forgetting_condenser.py` | Gradual forgetting over time |
| `BrowserOutputCondenser` | `impl/browser_output_condenser.py` | Compress browser observation output |
| `Pipeline` | `impl/pipeline.py` | Chain multiple condensers together |

### LLMSummarizingCondenser Detail

The most sophisticated condenser. When the view exceeds `max_size`, it:

1. Keeps the first `keep_first` events (prefix)
2. Identifies events to forget (middle section)
3. Calls an LLM to summarize the forgotten events
4. Returns a `Condensation` with a `CondensationAction`
5. The action is added to the event stream as an `AgentCondensationObservation`
6. On the next step, the condenser produces a `View` with the summary replacing forgotten events

### ConversationMemory

**File:** `openhands/memory/conversation_memory.py`

Transforms events into LLM messages. Handles:
- System message from `SystemMessageAction`
- Tool call/response pairing
- Message role alternation (user/assistant/tool)
- Anthropic prompt caching breakpoints
- Vision content inclusion
- Max message char truncation

### Agent-Initiated Condensation

The agent itself can request condensation via the `request_condensation` tool, returning a `CondensationRequestAction`. This triggers the condenser on the next step.

---

## 8. Agent Types

### Agent Registry

Agents self-register via `Agent.register(name, cls)`. The `agenthub/__init__.py` imports all agent modules to trigger registration.

### Implemented Agents

| Agent | File | Description |
|-------|------|-------------|
| **CodeActAgent** | `agenthub/codeact_agent/codeact_agent.py` | Primary agent. Based on the CodeAct paper. Uses function calling with bash, editor, IPython, browser, think, finish tools. |
| **BrowsingAgent** | `agenthub/browsing_agent/browsing_agent.py` | Specialized web browsing agent using BrowserGym. Used as a delegate from CodeAct. Processes accessibility tree (axtree) observations. |
| **VisualBrowsingAgent** | `agenthub/visualbrowsing_agent/visualbrowsing_agent.py` | Like BrowsingAgent but uses screenshots (vision) instead of axtree. |
| **ReadOnlyAgent** | `agenthub/readonly_agent/readonly_agent.py` | CodeAct variant with only read-only tools (grep, glob, view). For safe codebase exploration. |
| **LocAgent** | `agenthub/loc_agent/loc_agent.py` | CodeAct variant with localization-specific tools. Specialized for finding relevant code locations. |
| **DummyAgent** | `agenthub/dummy_agent/agent.py` | Test/mock agent. |

### Agent Delegation

The controller supports multi-agent delegation:

```python
# CodeAct delegates to BrowsingAgent for web tasks
action = AgentDelegateAction(agent='BrowsingAgent', inputs={'task': '...'})
```

The parent controller creates a child `AgentController` with `is_delegate=True`. The child shares the same `EventStream` but maintains its own `State`. When the delegate finishes, the parent receives an `AgentDelegateObservation`.

### Agent Base Class

```python
class Agent(ABC):
    _registry: dict[str, type['Agent']] = {}  # class-level registry
    sandbox_plugins: list[PluginRequirement] = []

    @abstractmethod
    def step(self, state: State) -> Action: ...

    @classmethod
    def register(cls, name: str, agent_cls: type['Agent']) -> None: ...
    @classmethod
    def get_cls(cls, name: str) -> type['Agent']: ...
```

---

## 9. Runtime

### Abstract Interface

**File:** `openhands/runtime/base.py`

```python
class Runtime(FileEditRuntimeMixin):
    """Abstract base class for agent runtime environments."""

    # Subscribes to EventStream
    # on_event() dispatches actions to execution methods
    # Methods: run(), run_ipython(), browse(), read(), write(), ...
```

The runtime subscribes to the `EventStream` as `EventStreamSubscriber.RUNTIME` and handles `Action` events.

### DockerRuntime (Primary)

**File:** `openhands/runtime/impl/docker/docker_runtime.py`

Inherits from `ActionExecutionClient` which inherits from `Runtime`.

Key behavior:
- Builds a custom Docker image layered on top of a base sandbox image
- Starts a container with the `action_execution_server.py` FastAPI server
- Uses `httpx` HTTP client to communicate with the container
- Port ranges: execution (30000-39999), VSCode (40000-49999), app ports (50000-59999)
- Retry logic with `tenacity` for container startup
- `LogStreamer` tails container logs for debugging

### Action Execution Server (Inside Container)

**File:** `openhands/runtime/action_execution_server.py`

This is the core execution engine that runs **inside** the Docker sandbox:

```python
# FastAPI app running inside the container
# Receives Action dicts via POST /execute_action
# Uses:
#   - BashSession for CmdRunAction
#   - JupyterPlugin for IPythonRunCellAction
#   - OHEditor (openhands-aci) for FileEditAction
#   - BrowserEnv (browsergym) for BrowseInteractiveAction
#   - MCPProxyManager for MCPAction
```

Components inside the container:
- `BashSession` -- persistent shell with soft timeout (10s default)
- `OHEditor` -- the `openhands-aci` file editor (str_replace, create, insert, undo)
- `BrowserEnv` -- Playwright-based browser via BrowserGym
- `JupyterPlugin` -- IPython kernel
- `MCPProxyManager` -- MCP server connections
- `MemoryMonitor` -- tracks container memory usage
- `FileViewerServer` -- serves files for browser viewing

### RemoteRuntime

For cloud deployments, the container runs on a remote server. The host communicates over HTTPS instead of localhost HTTP.

### LocalRuntime

For development, executes actions directly on the host machine without Docker isolation.

### KubernetesRuntime

Runs the sandbox as a Kubernetes pod. Used for scalable cloud deployments.

### CLIRuntime

Minimal runtime for CLI usage.

---

## 10. Web UI Backend

### Server Architecture (V0)

**File:** `openhands/server/app.py`

FastAPI application with Socket.IO for real-time communication:

```python
app = FastAPI(title='OpenHands', ...)

# REST API routes:
app.include_router(public_api_router)          # Public endpoints
app.include_router(files_api_router)           # File operations
app.include_router(security_api_router)        # Security settings
app.include_router(feedback_api_router)        # User feedback
app.include_router(conversation_api_router)    # Conversation management
app.include_router(manage_conversation_api_router)  # List/create conversations
app.include_router(settings_router)            # User settings
app.include_router(secrets_router)             # Secret management
app.include_router(git_api_router)             # Git operations
app.include_router(trajectory_router)          # Execution trajectory
```

### Socket.IO for Real-Time Events

**File:** `openhands/server/listen_socket.py`

```python
@sio.event
async def connect(connection_id, environ):
    # Validates conversation_id, session_api_key
    # Sets up event streaming to the connected client

# Events are streamed from EventStream to Socket.IO clients
# Client sends messages which become MessageAction events
```

### Session Management

**File:** `openhands/server/session/`

- `session.py` -- Session lifecycle
- `agent_session.py` -- Agent-specific session state
- `conversation.py` -- Conversation management (create, attach, detach)
- `conversation_init_data.py` -- Initialization data for new conversations

### MCP Server Endpoint

**File:** `openhands/server/routes/mcp.py`

The OpenHands server itself exposes an MCP endpoint at `/mcp` for external tools to interact with:

```python
mcp_app = mcp_server.http_app(path='/mcp', stateless_http=True)
```

### V1 App Server

**File:** `openhands/app_server/`

The new V1 application server is being built alongside the V0 server:

```
app_server/
├── app_conversation/    # Conversation management
├── app_lifespan/        # Application lifecycle
├── event/               # Event handling
├── event_callback/      # Event callbacks
├── sandbox/             # Sandbox management
├── services/            # Business logic services
├── user/                # User management
├── utils/               # Utilities
├── web_client/          # Web client
└── v1_router.py         # V1 API router
```

Conditionally included: `if server_config.enable_v1: app.include_router(v1_router.router)`

---

## 11. Microagent System

### Concept

Microagents are **knowledge modules** -- markdown files with frontmatter that inject context-specific instructions into the agent's prompt. They are OpenHands' equivalent of skills or project instructions.

**Files:**
- `openhands/microagent/microagent.py` -- Loading and parsing
- `openhands/microagent/types.py` -- Type definitions
- `openhands/memory/memory.py` -- Memory component that retrieves microagents
- `skills/` -- Global microagent files

### Microagent Types

```python
class MicroagentType(str, Enum):
    KNOWLEDGE = 'knowledge'      # Triggered by keywords
    REPO_KNOWLEDGE = 'repo'      # Always active (project-specific)
    TASK = 'task'                 # Triggered by /<name>, requires user input
```

### How Microagents Are Loaded

Three sources:
1. **Global** -- `skills/` directory (27 markdown files shipped with OpenHands)
2. **User** -- `~/.openhands/microagents/`
3. **Repository** -- `.openhands/microagents/` in the workspace repo

Additionally, third-party files are auto-detected:
- `.cursorrules` -> loaded as a repo microagent named "cursorrules"
- `AGENTS.md` / `agents.md` -> loaded as a repo microagent named "agents"

### Microagent Format

```markdown
---
name: github
type: knowledge
version: 1.0.0
agent: CodeActAgent
triggers:
- github
- git
---

You have access to an environment variable, `GITHUB_TOKEN`, which allows you to interact with
the GitHub API. ...
```

### Trigger-Based Retrieval

When a user sends a message, the `Memory` component creates a `RecallAction`:

```python
# In AgentController._handle_message_action():
recall_action = RecallAction(query=action.content, recall_type=recall_type)
self.event_stream.add_event(recall_action, EventSource.USER)
```

The `Memory` component listens for `RecallAction` events and:
1. For `WORKSPACE_CONTEXT` (first message): returns all repo microagents + runtime info + repository info
2. For `KNOWLEDGE` (subsequent messages): checks each knowledge microagent's triggers against the message text
3. Returns matched microagent content as a `RecallObservation`

### Recall Types

```python
class RecallType(str, Enum):
    WORKSPACE_CONTEXT = 'workspace_context'  # First user message
    KNOWLEDGE = 'knowledge'                   # Subsequent messages
```

### TaskMicroagent

A special type triggered by `/<agent_name>` that can require user input:

```python
class TaskMicroagent(KnowledgeMicroagent):
    def requires_user_input(self) -> bool:
        # Checks for ${variable_name} patterns in content
        variables = self.extract_variables(self.content)
        return len(variables) > 0
```

### Global Skills (shipped with OpenHands)

| Skill | Triggers | Purpose |
|-------|----------|---------|
| `github.md` | github, git | GitHub API interaction patterns |
| `gitlab.md` | gitlab | GitLab integration |
| `bitbucket.md` | bitbucket | Bitbucket integration |
| `docker.md` | docker | Docker best practices |
| `kubernetes.md` | kubernetes | K8s patterns |
| `npm.md` | npm | Node.js/npm patterns |
| `ssh.md` | ssh | SSH key management |
| `security.md` | security | Security practices |
| `code-review.md` | (task) | Code review workflow |
| `fix_test.md` | (task) | Test fixing workflow |
| `update_test.md` | (task) | Test updating workflow |
| `onboarding.md` | (task) | Repository onboarding |
| ... and 15 more | | |

---

## 12. Unique Features

### SWE-bench Issue Resolver

**File:** `openhands/resolver/`

A complete pipeline for automatically resolving GitHub issues. This is what makes OpenHands competitive on the SWE-bench leaderboard.

```
resolve_issue.py           # CLI entry point
issue_resolver.py          # Main resolver logic
issue_handler_factory.py   # Creates handlers for GitHub/GitLab issues
send_pull_request.py       # Automated PR creation
resolver_output.py         # Output formatting
```

The resolver:
1. Takes a repo + issue number
2. Clones the repo into the sandbox
3. Runs the agent with the issue as the task
4. Collects the resulting diff
5. Optionally creates a PR

### Stuck Detection (5 Scenarios)

**File:** `openhands/controller/stuck.py`

The `StuckDetector` identifies 5 distinct loop patterns:

1. **Repeating action/observation** -- Same action produces same observation 4 times
2. **Repeating action/error** -- Same action produces errors 3 times (including syntax errors in IPython)
3. **Monologue** -- Agent sends identical messages 3 times with no observations between
4. **Alternating pattern** -- A-B-A-B-A-B pattern over 6 steps
5. **Context window error loop** -- 10+ consecutive `AgentCondensationObservation` events (condenser failing to reduce enough)

### Loop Recovery

When stuck is detected, the agent can:
1. Restart from before the loop
2. Restart with the last user message
3. Stop completely

### Security Analyzer Framework

**File:** `openhands/security/`

Pluggable security analyzers that can gate agent actions:

| Analyzer | Description |
|----------|-------------|
| `invariant/` | Invariant-based safety checking |
| `grayswan/` | GraySwan security model |
| `llm/` | LLM-based risk assessment |

Actions carry `security_risk` levels (low/medium/high) and can trigger `AWAITING_USER_CONFIRMATION` state.

### openhands-aci (Agent-Computer Interface)

The file editing is powered by `openhands-aci`, a separate library that provides the `OHEditor` class. This is the same editor used in SWE-bench evaluations and supports:
- `view` -- cat with line numbers, or directory listing
- `create` -- create new file
- `str_replace` -- exact string replacement
- `insert` -- insert lines at position
- `undo_edit` -- revert last edit
- Binary file viewing (xlsx, pptx, pdf, docx, audio)
- Diff generation

### BrowserGym Integration

Browser interaction uses BrowserGym's high-level action set with Playwright under the hood:
- 15 high-level actions (goto, click, fill, scroll, etc.)
- Accessibility tree (axtree) rendering for text-based agents
- Screenshot rendering for vision-based agents (VisualBrowsingAgent)
- Multi-action support in a single turn

### MCP Integration

**File:** `openhands/mcp/`

- `client.py` -- MCP client for connecting to external tool servers
- `tool.py` -- MCP tool conversion to litellm ChatCompletionToolParam format
- Runtime-side `MCPProxyManager` handles MCP connections inside the sandbox
- Microagents can declare MCP server dependencies in their frontmatter

### Agent-Initiated Condensation

The agent can explicitly request context condensation via the `request_condensation` tool. This is novel -- most systems only condense automatically based on token limits.

### Task Tracking Tool

A built-in project management tool that lets the agent create and maintain task lists with statuses (todo/in_progress/done). This provides structured progress visibility to users.

### Model Routing

The `RouterLLM` system allows dynamic routing of requests to different models based on configurable rules, enabling cost optimization (e.g., use a cheaper model for simple tasks).

### Conversation Stats & Cost Tracking

**File:** `openhands/server/services/conversation_stats.py`

Tracks per-conversation metrics including:
- Token usage (input, output, cache read, cache write)
- Cost in USD
- Response latency
- Per-response breakdowns with response IDs

---

## Summary Comparison Notes (vs AVA)

| Aspect | OpenHands | AVA |
|--------|-----------|-----|
| Language | Python | TypeScript |
| Platform | Cloud-first (Docker sandbox) | Desktop-first (Tauri) |
| Agent pattern | Event-sourced, Action/Observation pairs | Extension-based, turn-based loop |
| Tool system | Typed Actions, litellm tool params | `defineTool()` with Zod schemas |
| Sandboxing | Docker container with HTTP API | Docker extension (optional) |
| LLM abstraction | litellm (single library) | 16 per-provider implementations |
| Context management | Pluggable condenser pipeline (9 strategies) | Compaction + prune strategy |
| Microagents/Skills | Markdown with frontmatter + keyword triggers | Auto-invoked by file globs/project type |
| Multi-agent | Delegation via AgentDelegateAction | Praxis hierarchy (Commander -> Leads -> Workers) |
| Extension system | None (monolithic) | ExtensionAPI with plugin ecosystem |
| Web UI | FastAPI + Socket.IO | SolidJS (Tauri webview) |
| Issue resolution | Built-in SWE-bench resolver pipeline | N/A |
| Browser | BrowserGym (Playwright) | Removed (users use Puppeteer MCP) |
