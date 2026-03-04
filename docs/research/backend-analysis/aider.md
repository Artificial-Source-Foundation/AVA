# Aider Backend Architecture Analysis

> Python CLI AI coding assistant (~41k GitHub stars). Known for repo mapping, edit format
> innovations, and git-native workflow.

---

## 1. Project Structure

```
aider/
├── aider/                     # Main Python package (~20K lines across ~50 files)
│   ├── coders/                # Edit format implementations (~38 files, ~6.5K lines)
│   │   ├── base_coder.py      # Core agent loop, message assembly, LLM interaction (2000+ lines)
│   │   ├── editblock_coder.py # SEARCH/REPLACE block format (the default "diff" format)
│   │   ├── wholefile_coder.py # Whole-file replacement format
│   │   ├── udiff_coder.py     # Unified diff format
│   │   ├── patch_coder.py     # V4A patch format (GPT-4.1 style)
│   │   ├── architect_coder.py # Two-model architect/editor pipeline
│   │   ├── ask_coder.py       # Read-only Q&A mode
│   │   ├── context_coder.py   # Auto-identifies files needing edits
│   │   ├── help_coder.py      # Help/documentation mode
│   │   ├── chat_chunks.py     # Message assembly data structure
│   │   ├── search_replace.py  # Shared fuzzy search/replace logic
│   │   ├── shell.py           # Shell command prompt templates
│   │   └── *_prompts.py       # Per-format prompt definitions
│   ├── queries/               # Tree-sitter tag query files (.scm)
│   │   ├── tree-sitter-language-pack/
│   │   └── tree-sitter-languages/
│   ├── resources/             # Static data files
│   │   ├── model-settings.yml # Per-model configuration (edit format, features, params)
│   │   └── model-metadata.json
│   ├── main.py                # CLI entry point, arg parsing, initialization
│   ├── commands.py            # All /slash commands (40+ commands, ~1700 lines)
│   ├── models.py              # Model abstraction, settings, info manager (~1400 lines)
│   ├── repo.py                # Git integration (GitRepo class)
│   ├── repomap.py             # Tree-sitter + PageRank repo mapping
│   ├── linter.py              # Post-edit linting (tree-sitter + flake8)
│   ├── sendchat.py            # Message validation utilities
│   ├── llm.py                 # Lazy litellm wrapper
│   ├── io.py                  # Terminal I/O (prompt_toolkit)
│   ├── voice.py               # Voice input via Whisper
│   ├── scrape.py              # Web scraping (Playwright/httpx + pandoc)
│   ├── watch.py               # File watcher for AI comments
│   ├── history.py             # Chat summarization (ChatSummary)
│   ├── analytics.py           # PostHog/Mixpanel telemetry
│   ├── editor.py              # External editor integration
│   ├── args.py                # CLI argument definitions
│   ├── prompts.py             # Shared prompt strings
│   ├── special.py             # Important file detection (175+ known files)
│   ├── reasoning_tags.py      # Reasoning content extraction
│   ├── run_cmd.py             # Shell command execution
│   ├── utils.py               # Utility functions
│   └── diffs.py               # Diff display utilities
├── benchmark/                 # Benchmarking suite
├── tests/                     # Test suite
├── scripts/                   # Build/release scripts
└── pyproject.toml             # Package config
```

**Key design principle:** Aider is a monolithic Python package. There is no plugin system,
extension API, or modular architecture. Everything lives in one flat namespace. The "coders"
directory is the closest thing to a module system -- each edit format is a self-contained
subclass of `Coder`.

**Total size:** ~20,262 lines across the `aider/` package and `aider/coders/` directory.

---

## 2. Commands

Aider uses `/slash` commands for user interaction. All commands are methods on the `Commands`
class in `commands.py`.

| Command | Method | Description |
|---------|--------|-------------|
| `/model` | `cmd_model` | Switch main LLM model |
| `/editor-model` | `cmd_editor_model` | Switch editor model (for architect mode) |
| `/weak-model` | `cmd_weak_model` | Switch weak model (for commit messages, summaries) |
| `/chat-mode` | `cmd_chat_mode` | Switch edit format / chat mode |
| `/models` | `cmd_models` | Search available models |
| `/web` | `cmd_web` | Scrape URL, convert to markdown, add to chat |
| `/commit` | `cmd_commit` | Commit pending changes |
| `/lint` | `cmd_lint` | Run linter on files |
| `/clear` | `cmd_clear` | Clear chat history |
| `/reset` | `cmd_reset` | Clear history and drop all files |
| `/tokens` | `cmd_tokens` | Show token usage breakdown |
| `/undo` | `cmd_undo` | Undo last aider commit |
| `/diff` | `cmd_diff` | Show diff of last aider changes |
| `/add` | `cmd_add` | Add files to chat |
| `/drop` | `cmd_drop` | Remove files from chat |
| `/git` | `cmd_git` | Run arbitrary git commands |
| `/test` | `cmd_test` | Run test command |
| `/run` | `cmd_run` | Run shell command, optionally add output |
| `/exit` | `cmd_exit` | Exit aider |
| `/quit` | `cmd_quit` | Exit aider (alias) |
| `/ls` | `cmd_ls` | List files in chat and repo |
| `/help` | `cmd_help` | Search aider documentation |
| `/ask` | `cmd_ask` | Switch to ask mode (no edits) |
| `/code` | `cmd_code` | Switch to code mode (model default) |
| `/architect` | `cmd_architect` | Switch to architect mode |
| `/context` | `cmd_context` | Switch to context mode (auto-file selection) |
| `/ok` | `cmd_ok` | Accept file watcher suggestions |
| `/voice` | `cmd_voice` | Record voice input via microphone |
| `/paste` | `cmd_paste` | Paste image/text from clipboard |
| `/read-only` | `cmd_read_only` | Add files as read-only references |
| `/map` | `cmd_map` | Show current repo map |
| `/map-refresh` | `cmd_map_refresh` | Force refresh repo map |
| `/settings` | `cmd_settings` | Show current settings |
| `/load` | `cmd_load` | Load commands from a file |
| `/save` | `cmd_save` | Save chat messages to a file |
| `/multiline-mode` | `cmd_multiline_mode` | Toggle multiline input |
| `/copy` | `cmd_copy` | Copy last assistant message to clipboard |
| `/report` | `cmd_report` | Report a problem |
| `/editor` | `cmd_editor` | Open external editor for input |
| `/edit` | `cmd_edit` | Edit files in external editor |
| `/think-tokens` | `cmd_think_tokens` | Set thinking token budget |
| `/reasoning-effort` | `cmd_reasoning_effort` | Set reasoning effort level |
| `/copy-context` | `cmd_copy_context` | Copy context from clipboard |

**Shell commands:** Lines starting with `!` are passed directly to the shell via `cmd_run`.

---

## 3. Agent Loop

The core loop lives in `base_coder.py` in the `Coder` class. It is **not** a tool-calling
agent loop. Aider uses a simpler "chat and parse" architecture.

### High-Level Flow

```
User Input
    |
    v
run_one(message)
    |
    +-- preproc_user_input()     # Check for /commands, URL mentions, file mentions
    |
    +-- send_message(message)    # Main LLM interaction
    |   |
    |   +-- format_messages()    # Assemble ChatChunks (system + examples + repo + files + history)
    |   +-- check_tokens()       # Verify context window fit
    |   +-- warm_cache()         # Background cache keep-alive (Anthropic)
    |   +-- model.send_completion()  # Call LLM via litellm
    |   +-- show_send_output_stream() / show_send_output()  # Stream/display response
    |   +-- check_for_file_mentions()  # Detect if LLM mentions files not in chat
    |   +-- reply_completed()    # Format-specific post-processing (architect delegates here)
    |   +-- apply_updates()      # Parse edits from response, write to disk
    |   +-- auto_commit()        # Git commit the changes
    |   +-- lint_edited()        # Run linter on changed files
    |   +-- auto_test()          # Run test command
    |
    +-- reflected_message?       # If lint/test fails, loop back with error
    |   (up to max_reflections=3)
```

### Message Assembly (ChatChunks)

Messages are assembled in a specific order via `format_chat_chunks()`:

```python
class ChatChunks:
    system          # System prompt (edit format instructions, examples)
    examples        # Few-shot example conversations
    readonly_files  # Read-only file contents
    repo            # Repo map (tree-sitter ranked tags)
    done            # Previous conversation history (may be summarized)
    chat_files      # Editable file contents (full source)
    cur             # Current user message
    reminder        # System reminder (appended at end for recency)
```

**File:** `aider/coders/chat_chunks.py`

### Cache Control

For Anthropic models, aider injects `cache_control: {"type": "ephemeral"}` headers at
strategic breakpoints (after examples, after repo map, after chat files) to enable prompt
caching. A background thread sends periodic cache-warming pings every ~5 minutes.

**File:** `aider/coders/base_coder.py` (`warm_cache()`, `ChatChunks.add_cache_control_headers()`)

### Reflection Loop

When edits fail to parse, lint fails, or tests fail, the error message is set as
`self.reflected_message` and the loop retries (up to `max_reflections=3`):

```python
while message:
    self.reflected_message = None
    list(self.send_message(message))
    if not self.reflected_message:
        break
    if self.num_reflections >= self.max_reflections:
        break
    self.num_reflections += 1
    message = self.reflected_message
```

### Infinite Output

When the LLM hits `finish_reason: "length"` and the model supports assistant prefill,
aider appends the partial response as an assistant message and continues streaming:

```python
except FinishReasonLength:
    if not self.main_model.info.get("supports_assistant_prefill"):
        exhausted = True
        break
    self.multi_response_content = self.get_multi_response_content_in_progress()
    messages.append(dict(role="assistant", content=self.multi_response_content, prefix=True))
```

---

## 4. Edit Formats

This is Aider's key innovation. Each edit format is a `Coder` subclass with its own:
- `edit_format` string identifier
- `gpt_prompts` class with system prompts and examples
- `get_edits()` method to parse LLM response
- `apply_edits()` method to write changes to disk

### Format Registry

All formats are registered in `aider/coders/__init__.py`:

```python
__all__ = [
    HelpCoder,           # "help" -- Documentation Q&A
    AskCoder,            # "ask" -- Read-only Q&A
    EditBlockCoder,      # "diff" -- SEARCH/REPLACE blocks (DEFAULT)
    EditBlockFencedCoder,# "diff-fenced" -- SEARCH/REPLACE with forced fencing
    WholeFileCoder,      # "whole" -- Full file replacement
    PatchCoder,          # "patch" -- V4A patch format
    UnifiedDiffCoder,    # "udiff" -- Unified diff format
    UnifiedDiffSimpleCoder, # "udiff-simple" -- Simplified unified diff
    ArchitectCoder,      # "architect" -- Two-model pipeline
    EditorEditBlockCoder,   # "editor-diff" -- SEARCH/REPLACE for editor subagent
    EditorWholeFileCoder,   # "editor-whole" -- Whole file for editor subagent
    EditorDiffFencedCoder,  # "editor-diff-fenced" -- Fenced diff for editor subagent
    ContextCoder,        # "context" -- Auto file selection
]
```

### Format Details

#### `diff` (SEARCH/REPLACE blocks) -- Default

**File:** `aider/coders/editblock_coder.py`

The LLM outputs SEARCH/REPLACE blocks that look like:

```
filename.py
<<<<<<< SEARCH
old code to find
=======
new replacement code
>>>>>>> REPLACE
```

**Matching strategy (in order):**
1. Perfect string match (`perfect_replace()`)
2. Whitespace-flexible match (`replace_part_with_missing_leading_whitespace()`) -- handles
   LLMs that mess up leading whitespace
3. Dotdotdot expansion (`try_dotdotdots()`) -- handles `...` elision in SEARCH blocks
4. Fuzzy match (disabled by default) (`replace_closest_edit_distance()`) -- SequenceMatcher
   with 0.8 similarity threshold

**Error recovery:** When a SEARCH block fails to match, the coder tries patching every other
file in the chat. If it still fails, it returns the original content with "Did you mean?"
suggestions using `find_similar_lines()`.

**Shell command detection:** Shell code blocks (` ```bash `) are extracted separately and
yielded as shell commands.

#### `whole` (Full file replacement)

**File:** `aider/coders/wholefile_coder.py`

The LLM outputs complete file contents in fenced code blocks. The filename is detected from
the line immediately before the opening fence. Files are completely overwritten.

**Tradeoff:** Simple and reliable for small files. Wasteful of tokens for large files.
Used by weaker models that struggle with SEARCH/REPLACE formatting.

#### `udiff` (Unified diff)

**File:** `aider/coders/udiff_coder.py`

The LLM outputs standard unified diffs inside ` ```diff ` blocks. Hunks are parsed and
applied using a multi-strategy approach:

1. Direct hunk application (`directly_apply_hunk()`)
2. Flexible search/replace on before/after text
3. Partial hunk application (`apply_partial_hunk()`) -- applies context/change sections
   independently with decreasing context requirements

**Tradeoff:** More token-efficient than whole-file. But LLMs frequently produce malformed
diffs, making this less reliable in practice.

#### `patch` (V4A format)

**File:** `aider/coders/patch_coder.py`

A structured patch format inspired by OpenAI's GPT-4.1 apply_patch format:

```
*** Begin Patch
*** Update File: path/to/file.py
@@ function_scope
 context line
-deleted line
+added line
*** End Patch
```

Supports three action types:
- `*** Add File:` -- create new files
- `*** Update File:` -- modify existing files (with optional `*** Move to:`)
- `*** Delete File:` -- remove files

**Context matching** uses multi-level fuzz: exact -> rstrip -> strip, with fuzz tracking.
Scope lines (`@@`) help navigate to the right location in large files.

#### `architect` (Two-model pipeline)

**File:** `aider/coders/architect_coder.py`

The architect model (usually a stronger/larger model) describes changes in natural language.
Then an editor model (usually a smaller/cheaper model) implements those changes:

```python
class ArchitectCoder(AskCoder):
    def reply_completed(self):
        content = self.partial_response_content  # architect's natural language plan
        editor_coder = Coder.create(
            main_model=editor_model,
            edit_format=self.main_model.editor_edit_format,
            from_coder=self,
        )
        editor_coder.run(with_message=content, preproc=False)
```

The architect coder extends `AskCoder` (no editing), and delegates to a fresh `Coder`
instance with the editor model. The editor gets the architect's output as its user message.

**Tradeoff:** More expensive (two LLM calls) but higher quality. The architect can reason
freely while the editor focuses on precise code changes.

#### `context` (Auto file selection)

**File:** `aider/coders/context_coder.py`

Specialized mode that asks the LLM to identify which files need editing. It amplifies the
repo map (multiplied by `map_mul_no_files`), always refreshes it, and then checks if the
LLM's file mentions differ from the current chat set. Iterates up to `max_reflections`
times to converge.

#### Editor Variants

`editor-diff`, `editor-whole`, `editor-diff-fenced` are stripped-down versions of their
parent formats, used as the implementation backend for architect mode. They have simplified
prompts focused purely on code editing.

---

## 5. Repo Map

**Files:** `aider/repomap.py` (~870 lines)

The repo map is Aider's most famous feature. It provides the LLM with a condensed view of
the entire repository -- showing which files exist and what identifiers (functions, classes,
variables) they define, ranked by relevance.

### Architecture

```
Tree-sitter Parsing
    |
    v
Tag Extraction (definitions + references)
    |
    v
NetworkX MultiDiGraph Construction
    |
    v
PageRank with Personalization
    |
    v
Ranked Tag Selection
    |
    v
TreeContext Rendering (grep-ast)
    |
    v
Binary Search for Token Budget
```

### Step 1: Tag Extraction

```python
Tag = namedtuple("Tag", "rel_fname fname line name kind")
```

For each file in the repo, aider uses **tree-sitter** queries to extract:
- **Definitions** (`name.definition.*`): function names, class names, variable names
- **References** (`name.reference.*`): usages of those identifiers

Tag queries are stored as `.scm` files in `aider/queries/tree-sitter-language-pack/` and
`aider/queries/tree-sitter-languages/`.

**Fallback:** When tree-sitter only provides definitions (e.g., C++), aider uses
**Pygments** lexer tokens as a fallback for references.

**Caching:** Tags are cached per-file using `diskcache.Cache` (SQLite-backed) with mtime
invalidation. Cache directory: `.aider.tags.cache.v4/`.

### Step 2: Graph Construction

```python
G = nx.MultiDiGraph()

for ident in idents:
    for referencer in references[ident]:
        for definer in defines[ident]:
            G.add_edge(referencer, definer, weight=mul * num_refs, ident=ident)
```

**Nodes** are filenames. **Edges** go from files that reference an identifier to files that
define it. Edge weights are influenced by:

| Factor | Weight Multiplier |
|--------|------------------|
| Mentioned in current user message | 10x |
| Snake_case, kebab-case, or camelCase with length >= 8 | 10x |
| Starts with underscore (private) | 0.1x |
| Defined in > 5 files (generic) | 0.1x |
| Referenced from a file in the chat | 50x |
| Number of references | sqrt(count) (diminishing returns) |

### Step 3: PageRank

```python
personalization = {}
# Files in chat get boosted personalization
for fname in chat_fnames:
    personalization[rel_fname] = personalize  # 100 / num_files

ranked = nx.pagerank(G, weight="weight", personalization=personalization, dangling=personalization)
```

PageRank with **personalization vector** biased toward:
- Files currently in the chat
- Files mentioned by name in the user message
- Files whose path components match mentioned identifiers

### Step 4: Ranked Tag Map Generation

The output is a text representation of the most relevant files and their definitions:

```
src/utils.py:
    def parse_config(...)
    class Logger:
        def info(...)

src/main.py:
    def main(...)
```

**Token budget fitting:** Uses binary search over the number of ranked tags to include,
targeting the configured `max_map_tokens` (default 1024) within a 15% error margin.

**TreeContext rendering** (from `grep-ast` library) shows relevant lines-of-interest with
surrounding scope context, so the LLM sees function signatures and class definitions without
full implementations.

### Refresh Strategies

| Strategy | Behavior |
|----------|----------|
| `auto` (default) | Cache if map generation takes > 1 second |
| `always` | Regenerate every turn |
| `files` | Cache based on file set |
| `manual` | Only refresh on `/map-refresh` |

### No-Files Amplification

When no files are in the chat, the repo map budget is multiplied by `map_mul_no_files`
(default 8x) to give the LLM a broader view of the codebase.

---

## 6. LLM Providers

Aider uses **litellm** as its universal LLM abstraction layer. All provider communication
goes through a single call path.

### Lazy Loading

```python
# aider/llm.py
class LazyLiteLLM:
    """litellm takes 1.5 seconds to import -- defer it."""
    _lazy_module = None

    def __getattr__(self, name):
        self._load_litellm()
        return getattr(self._lazy_module, name)
```

### Model Configuration

**File:** `aider/models.py`

Each model has a `ModelSettings` dataclass:

```python
@dataclass
class ModelSettings:
    name: str
    edit_format: str = "whole"           # Default edit format for this model
    weak_model_name: Optional[str]       # Model for commit messages, summaries
    use_repo_map: bool = False           # Whether to include repo map
    lazy: bool = False                   # Add "implement everything" reminder
    overeager: bool = False              # Add "don't over-edit" reminder
    reminder: str = "user"               # "sys" or "user" for reminder placement
    examples_as_sys_msg: bool = False    # Fold examples into system message
    extra_params: Optional[dict]         # Provider-specific params
    cache_control: bool = False          # Enable prompt caching
    caches_by_default: bool = False      # Model caches automatically
    use_system_prompt: bool = True       # Use system role vs user role
    use_temperature: Union[bool, float]  # Temperature setting
    streaming: bool = True               # Enable streaming
    editor_model_name: Optional[str]     # Model for architect's editor
    editor_edit_format: Optional[str]    # Edit format for editor
    reasoning_tag: Optional[str]         # Tag for reasoning content
    system_prompt_prefix: Optional[str]  # Prefix for all system prompts
    accepts_settings: Optional[list]     # Model-specific settings support
```

Settings are loaded from `aider/resources/model-settings.yml` with ~50+ model
configurations.

### Model Aliases

```python
MODEL_ALIASES = {
    "sonnet": "claude-sonnet-4-5",
    "haiku": "claude-haiku-4-5",
    "opus": "claude-opus-4-6",
    "4o": "gpt-4o",
    "deepseek": "deepseek/deepseek-chat",
    "gemini": "gemini/gemini-3-pro-preview",
    "flash": "gemini/gemini-flash-latest",
    "r1": "deepseek/deepseek-reasoner",
    "grok3": "xai/grok-3-beta",
    # ...
}
```

### Three Model Roles

1. **Main model** -- primary coding model
2. **Weak model** -- for commit messages, chat summarization (defaults to `gpt-4o-mini`)
3. **Editor model** -- for architect mode implementation (defaults to main model's editor setting)

### Model Info Resolution

Model metadata (context window, pricing, capabilities) is resolved through a waterfall:

1. Local `model-settings.yml` from package resources
2. Cached litellm model database (`~/.aider/caches/model_prices_and_context_window.json`)
3. Live litellm API call
4. OpenRouter API for `openrouter/` prefixed models
5. Web scraping of openrouter.ai pages (last resort)

### Supported Providers

Through litellm, Aider supports all providers that litellm supports:
- OpenAI, Azure OpenAI, Anthropic, Google/Gemini, DeepSeek, xAI (Grok)
- OpenRouter (as a meta-provider)
- Ollama, LM Studio, vLLM (local models)
- AWS Bedrock, Google Vertex AI
- Any OpenAI-compatible endpoint

### Send Path

```python
# aider/models.py - Model.send_completion()
hash_object, completion = model.send_completion(messages, functions, stream, temperature)

# Internally calls:
litellm.completion(
    model=self.name,
    messages=messages,
    stream=stream,
    temperature=temperature,
    **self.extra_params,
)
```

---

## 7. Context / Token Management

### Token Counting

Aider uses litellm's token counting. For large text (> 200 chars), it samples lines
to estimate tokens more efficiently:

```python
def token_count(self, text):
    if len(text) < 200:
        return self.main_model.token_count(text)
    # Sample ~1% of lines, extrapolate
    step = num_lines // 100 or 1
    sample_text = "".join(lines[::step])
    est_tokens = sample_tokens / len(sample_text) * len_text
    return est_tokens
```

### Context Window Budget

The context is assembled in priority order (see ChatChunks). When approaching limits:

1. **Repo map auto-sizes:** if no files in chat, repo map gets 8x more tokens
2. **Reminder omission:** system reminder is dropped if it would exceed `max_input_tokens`
3. **Chat history summarization:** when `done_messages` exceed `max_chat_history_tokens`,
   they are summarized in a background thread
4. **User warning:** if estimated tokens >= max_input_tokens, user is warned with
   suggestions to `/drop` files or `/clear` history

### Chat History Summarization

**File:** `aider/history.py`

```python
class ChatSummary:
    def summarize(self, messages):
        # Split messages: keep recent tail, summarize older head
        # Head is sent to weak model with summarization prompt
        # Recursively summarize if still too large (up to depth 3)
```

Summarization runs in a background thread after each turn. It splits the conversation
at a point where the tail fits in `max_tokens / 2`, summarizes the head using the weak
model, and replaces `done_messages` with the summary.

### Read-Only Files

Files added via `/read-only` are included in the context but the LLM is instructed not
to edit them. They appear in `readonly_files` messages with the prompt:
"Here are some READ ONLY files, provided for your reference. Do not edit these files!"

### Image Support

Images (PNG, JPG) and PDFs are base64-encoded and included as `image_url` content blocks
if the model supports vision/PDF input.

---

## 8. Git Integration

**File:** `aider/repo.py` (GitRepo class, ~620 lines)

Aider is deeply git-native. Git is not optional -- it is fundamental to the workflow.

### Auto-Commits

After every successful edit, aider automatically commits the changes:

```python
def auto_commit(self, edited):
    # 1. Commit any dirty files that existed before the edit (dirty commit)
    # 2. Commit the aider-edited files with LLM-generated message
    context = self.get_context_from_history(self.cur_messages)
    commit_hash, commit_message = self.repo.commit(
        fnames=edited,
        context=context,
        aider_edits=True,
    )
```

### Commit Message Generation

Uses the weak model to generate commit messages from diffs:

```python
def get_commit_message(self, diffs, context):
    messages = [
        dict(role="system", content=self.commit_prompt or prompts.commit_system),
        dict(role="user", content=context + diffs),
    ]
    commit_message = model.simple_send_with_retries(messages)
```

### Attribution

Aider modifies git author/committer names to track AI-generated changes:

| Setting | Effect |
|---------|--------|
| `--attribute-author` | Author becomes "Name (aider)" |
| `--attribute-committer` | Committer becomes "Name (aider)" |
| `--attribute-co-authored-by` | Adds `Co-authored-by: aider (model) <aider@aider.chat>` trailer |
| `--attribute-commit-message-author` | Prefixes message with "aider: " |

### Undo

The `/undo` command reverts the last aider commit. Aider tracks its own commit hashes in
`self.aider_commit_hashes` to distinguish its commits from user commits.

### Dirty File Handling

Before making edits, aider commits any already-dirty files as a separate "dirty commit"
so aider's changes are isolated in their own commits.

### .aiderignore

Similar to `.gitignore`, the `.aiderignore` file excludes files from aider's view.
It uses `pathspec` with GitWildMatch patterns.

---

## 9. Architect Mode

**Files:** `aider/coders/architect_coder.py`, `aider/coders/architect_prompts.py`

Architect mode implements a two-model pipeline:

### Flow

```
User Request
    |
    v
Architect Model (strong, e.g. Claude Opus)
    - Receives: full context, repo map, file contents
    - Outputs: natural language description of changes
    - Prompt: "Act as an expert architect engineer..."
    - "DO NOT show the entire updated function/file/etc!"
    |
    v
[Optional] User confirmation ("Edit the files?")
    |
    v
Editor Model (fast, e.g. Claude Haiku)
    - Receives: architect's plan as user message
    - Uses: its own edit format (diff, whole, etc.)
    - Creates: actual code changes
    - Has: same file context, no repo map, no cache
```

### Implementation Details

```python
class ArchitectCoder(AskCoder):
    edit_format = "architect"

    def reply_completed(self):
        content = self.partial_response_content  # architect's plan

        editor_coder = Coder.create(
            main_model=self.main_model.editor_model,
            edit_format=self.main_model.editor_edit_format,
            suggest_shell_commands=False,
            map_tokens=0,              # No repo map for editor
            cache_prompts=False,       # No caching for editor
            summarize_from_coder=False,
            from_coder=self,
        )
        editor_coder.cur_messages = []
        editor_coder.done_messages = []
        editor_coder.run(with_message=content, preproc=False)

        self.move_back_cur_messages("I made those changes to the files.")
```

Key points:
- The editor model gets no chat history (`cur_messages=[]`, `done_messages=[]`)
- The editor gets no repo map (`map_tokens=0`)
- The editor gets no prompt caching
- The editor gets no shell command suggestions
- The architect's full output becomes the editor's sole user message
- Costs accumulate across both models

### Model Configuration

Default editor models are specified per-model in `model-settings.yml`. For example:
- Claude Sonnet might use Claude Haiku as editor
- GPT-4o might use GPT-4o-mini as editor

---

## 10. Linting / Validation

**File:** `aider/linter.py` (~305 lines)

### Multi-Layer Linting

Aider runs linting after every edit when `auto_lint=True`:

```
tree-sitter syntax check (all languages)
    |
    v
Python compile() check (Python only)
    |
    v
flake8 fatal errors (Python only, E9/F821/F823/F831/F406/F407/F701/F702/F704/F706)
    |
    v
User-configured lint commands (any language)
```

### Tree-sitter Linting

```python
def basic_lint(fname, code):
    parser = get_parser(lang)
    tree = parser.parse(bytes(code, "utf-8"))
    errors = traverse_tree(tree.root_node)  # Find ERROR nodes
```

This catches syntax errors in any language that tree-sitter supports.

### Error Presentation

Lint errors are displayed with **TreeContext** from `grep-ast`, showing the error lines
marked with `|` in context with surrounding scope:

```
## See relevant lines below marked with |.

src/utils.py:
|   def broken_function(
|       x,
|       y
|   )  # missing colon
```

### Auto-Fix Loop

When linting detects errors:
1. The error output is shown to the user
2. If the user confirms, the errors become `self.reflected_message`
3. The agent loop retries with the lint errors as input
4. Up to 3 reflection attempts

### Test Integration

When `auto_test=True`, aider runs the configured test command after edits:
```python
test_errors = self.commands.cmd_test(self.test_cmd)
if test_errors:
    self.reflected_message = test_errors  # retry with test output
```

---

## 11. Unique Features

### Voice Coding

**File:** `aider/voice.py` (~188 lines)

Records audio from microphone using `sounddevice`, converts to WAV/MP3/WebM, transcribes
via OpenAI's Whisper API:

```python
transcript = litellm.transcription(model="whisper-1", file=fh, prompt=history, language=language)
```

Features:
- Real-time RMS level visualization during recording
- Automatic format conversion if WAV exceeds 25MB
- Optional device selection
- Language detection from previous chat context

### Web Scraping

**File:** `aider/scrape.py` (~285 lines)

Two scraping backends:
1. **Playwright** (preferred): headless Chromium, handles JavaScript-rendered pages
2. **httpx** (fallback): simple HTTP GET

HTML is converted to markdown via **pypandoc** (pandoc wrapper). The HTML is first
slimmed down by BeautifulSoup (removing SVGs, images, data URIs, non-href attributes).

### File Watcher (AI Comments)

**File:** `aider/watch.py` (~318 lines)

Watches source files for special AI comment markers:

```python
# AI please refactor this function
# AI!                    (triggers code changes)
# AI?                    (triggers questions)
```

The watcher uses `watchfiles` library with gitignore-aware filtering. When it detects
an AI comment, the file is automatically added to the chat and changes are processed.

### Analytics

**File:** `aider/analytics.py` (~258 lines)

Opt-in telemetry via PostHog (primary) and Mixpanel (secondary). Tracks:
- Events (message sends, model usage, commands)
- System info (OS, Python version, aider version)
- Model names (redacted if not in known model database)

UUID-based user identification stored in `~/.aider/analytics.json`. Only sampled to
10% of users for the opt-in prompt.

### Chat Summarization

**File:** `aider/history.py`

Background thread summarizes old chat messages to stay within token limits.
Uses a recursive split-and-summarize strategy:
1. Split messages into head (old) and tail (recent)
2. Summarize head using weak model
3. If combined result still too large, recurse (up to depth 3)

### Fence Selection

Aider automatically chooses the code fence style to avoid conflicts with file content:

```python
all_fences = [
    ("```", "```"),
    ("````", "````"),
    ("<source>", "</source>"),
    ("<code>", "</code>"),
    ("<pre>", "</pre>"),
    ("<codeblock>", "</codeblock>"),
    ("<sourcecode>", "</sourcecode>"),
]
```

If any file contains triple-backtick fences, aider switches to XML-style fences.

### Important File Boosting

**File:** `aider/special.py`

Aider maintains a list of 175+ commonly important files (README, package.json,
Dockerfile, etc.) and boosts them in the repo map ranking. These files appear at the
top of the repo map regardless of PageRank score.

### Model-Specific Prompt Tuning

The system prompt adapts based on model characteristics:
- `lazy` models get: "You NEVER leave comments describing code without implementing it!"
- `overeager` models get: "Do what they ask, but no more."
- Reminder placement varies: some models need system-role reminders, others user-role
- Examples can be folded into the system message for models with small context windows

### Reasoning Content Handling

**File:** `aider/reasoning_tags.py`

For models that output reasoning traces (DeepSeek R1, o1/o3-mini), aider:
1. Detects `reasoning_content` or `reasoning` fields in streaming chunks
2. Wraps them in configurable XML tags (e.g., `<think>...</think>`)
3. Strips them from the final response before parsing edits

### Clipboard Integration

**File:** `aider/copypaste.py`

A `ClipboardWatcher` monitors the system clipboard for changes and can automatically
paste content into the chat context.

### Configuration Cascade

Aider reads configuration from multiple sources in order:
1. `~/.aider.conf.yml` (global)
2. `.aider.conf.yml` (project-level)
3. Environment variables (`AIDER_*`)
4. Command-line arguments

---

## Summary of Key Architectural Decisions

| Decision | Aider's Approach |
|----------|-----------------|
| **Architecture** | Monolithic Python package, no plugins |
| **Agent Loop** | Chat-and-parse, not tool-calling |
| **Edit Application** | LLM writes edits in text format, parsed by format-specific code |
| **Provider Abstraction** | litellm (single dependency for all providers) |
| **Context Management** | Manual file management (`/add`, `/drop`) + auto repo map |
| **Git Integration** | Deep, mandatory, auto-commit after every edit |
| **Code Quality** | Tree-sitter linting + optional flake8 + user-defined commands |
| **Repo Understanding** | Tree-sitter tags + NetworkX PageRank |
| **Multi-Model** | Three roles (main, weak, editor) not N arbitrary agents |
| **State Management** | All in-memory on the `Coder` instance, no persistent sessions |
| **Token Optimization** | Prompt caching, background summarization, sampling-based counting |
