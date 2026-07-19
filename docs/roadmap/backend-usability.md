# AVA Backend Usability Approval Ledger

This document is the single approval ledger for the backend-usability work identified during AVA dogfooding. Each workstream is researched, discussed, approved, and implemented independently when the user authorizes it. The approved result and implementation status are recorded here before moving to the next workstream.

External projects are behavior references only. AVA may adopt useful product behavior without copying their architecture or source.

## Approval Workflow

1. Research one workstream in AVA and relevant reference projects.
2. Explain the findings and proposed AVA behavior.
3. Discuss tradeoffs until the user approves a direction.
4. Record only the approved direction in this ledger.
5. Repeat for the next workstream.
6. Implement only the approved workstream when the user explicitly authorizes it.
7. Validate and record the result before researching the next workstream.

## Workstream Status

| # | Workstream | Status | Implementation |
| --- | --- | --- | --- |
| 1 | Normal coding-command permissions and execution | Direction approved 2026-07-19; recorded below | Implemented and validated 2026-07-19 |
| 2 | Background subagent job controls | Awaiting focused research and discussion | Not started |
| 3 | Privacy-safe diagnostics and support bundles | Awaiting focused research and discussion | Not started |
| 4 | Context-compaction correctness and configuration | Awaiting focused research and discussion | Not started |
| 5 | Automatic session titles | Awaiting focused research and discussion | Not started |

---

# Workstream 1: Coding-Command Permissions And Execution

## Implementation Record

Implemented on `develop` on 2026-07-19. AVA now seals one canonical command plan before permission evaluation and uses that same executable identity, argv/raw-shell payload, working directory, environment profile, and capability classification for execution. Bare and absolute executable spellings no longer take different policy paths.

The backend provides Standard, Sensitive, and Critical command levels. Recognized read-only inspection runs automatically. Recognized project builds/tests can run automatically only under the verified Linux development containment profile; otherwise they downgrade to a prompt. Sensitive and Critical commands ask rather than being rejected solely by executable name, while backend-owned maximum scopes prevent a frontend from widening one-time authority.

Schema-v2 global and external workspace permission rules use opaque typed recipe identities rather than raw wildcard command text. Matching Denies are checked before Standard auto-Allow, including model-initiated foreground/background subagent commands. TUI supports allow once, allow for the current AVA session, and always in the workspace; RPC exposes bounded session grants; global rules remain an explicit user-managed choice. ACP cannot reuse local recipe authority while its executor identity is unverified.

Local children use a positive-list, secret-free environment and filtered system/user/workspace toolchain discovery. A safe exact `<trusted_home>/.rustup` root is identity-sealed and exposed read-only through `RUSTUP_HOME` so common rustup-backed Cargo commands work; real `HOME`, `CARGO_HOME`, Cargo credentials, arbitrary inherited toolchain variables, and network remain unavailable. Linux Landlock plus seccomp network denial provide the verified development profile, including descendant inheritance; unsupported kernels and unverified delegated executors downgrade rather than claiming containment. AVA config/session/auth roots are excluded from command workspaces and synthetic child environment roots.

The implementation intentionally keeps checked-in `.ava/permission-rules.json` files non-authoritative and ignored; project-owned files cannot grant their own execution authority. Print mode remains fail-closed for unresolved prompts, while external global/workspace rules and RPC replies provide explicit automation authority. A dedicated `--allow-command` convenience flag remains a possible follow-up rather than adding blanket `--allow-tool bash`.

Primary implementation areas are `src/ava/command/`, `src/ava/containment/`, `src/ava/permissions/`, `src/ava/tools/bash_tool.cpp`, runtime model-command wiring, TUI/RPC/ACP permission resolvers, and the corresponding command/containment/permission/frontend tests.

## Validation Record

Validation used no paid live-provider calls.

- The normal configured CTest suite passed all 77 executed checks out of 94 registered checks; the 17 skips were the credential-gated provider check, optional official ACP SDK check, 13 opt-in tmux scenarios, and the Kitty-image/OSC-8 terminal smokes.
- The sanitizer CTest suite passed the same 77 executed checks with zero failures in 205.99 seconds under the intentional two-job cap.
- All 13 opt-in tmux TUI scenarios then executed concurrently and passed in 12.74 seconds. This includes raw-shell one-shot prompting, exact persistent denial, redacted durable audit/diagnostics, session-grant behavior, and fixture authority permissions.
- Focused command, containment, tool/process, AgentLoop, permission-rule, runtime, RPC, ACP, and TUI tests passed. They cover Standard auto-Allow and deny preflight, mutable-project containment, one-shot Critical behavior, bare/absolute and trusted symlink-alias identity parity, private-primary-group workspaces, typed recipe scope separation, secret-free environments, sealed read-only rustup state, and foreground/background subagent routes.
- A credential-free RPC smoke ran a real rustup-backed `cargo check` with no permission event under verified containment. Cargo reached the expected missing-`Cargo.toml` error, proving the symlink alias and sealed `RUSTUP_HOME` path work without exposing `CARGO_HOME`.
- The package test, Python smoke-driver compilation, `clang-format --dry-run --Werror` on changed C++, and `git diff --check` passed.
- One final integrated security/correctness gate reviewed the complete command path under the documented malicious-model/repository threat model and reported no blocking findings.

## Approved Product Direction

AVA will use an OpenCode-like permission experience without adopting OpenCode's blanket shell authority. Most recognized, contained, normal development commands should run without prompting. Commands with sensitive or difficult-to-predict effects should ask the user rather than being categorically forbidden. The more authority a command needs, the narrower and more explicit its approval becomes.

The user remains the final authority. A valid critical command is not rejected merely because of its executable name. Commands that AVA cannot safely classify or contain are presented as critical and require informed approval. Malformed requests, ambiguous argument encoding, invalid paths, stale permission identities, and failed containment remain fail-closed implementation errors rather than user-overridable command policy.

## Verified Reference Behavior

The comparison used these pinned local references:

- Pi: `d53b567601da35db61208418da956a43275ccf28`
- OpenCode: `7a8e7c88f495acf5af3e7584e8ec1dbab2fe04ec`

### OpenCode

OpenCode's effective default build-agent permission begins with `"*": "allow"`. Consequently, normal tool and shell calls do not prompt by default. Its notable default exceptions include:

- external-directory access: ask;
- reading `.env`-style files: ask, except `.env.example`;
- repeated identical tool calls (doom-loop detection): ask.

OpenCode supports `allow`, `ask`, and `deny` rules, command patterns, global configuration, project configuration, and an in-memory "always" response for the active instance. Its shell scanner derives human-readable reusable patterns such as a specific package-manager script or Git subcommand.

OpenCode's convenience comes with broad authority: it runs a real shell, inherits the host environment, and grants shell commands the host user's filesystem, process, and network authority. Project configuration can also contribute permission rules. AVA will adopt the low-friction permission experience, not those trust assumptions.

Primary reference locations:

- OpenCode defaults: `docs/reference-code/opencode/packages/opencode/src/agent/agent.ts`
- Permission evaluation and replies: `docs/reference-code/opencode/packages/opencode/src/permission/index.ts`
- Shell scanning and reusable patterns: `docs/reference-code/opencode/packages/opencode/src/tool/shell.ts`
- Global/project config loading: `docs/reference-code/opencode/packages/opencode/src/config/config.ts`

### Pi

Pi prioritizes direct terminal ergonomics. Its built-in Bash tool executes commands without a built-in command classifier or approval prompt, inherits the process environment and PATH, and relies on optional extensions to block or replace tool calls. It has strong cancellation, process-tree cleanup, and output truncation, but its default command-authority model is intentionally more permissive than AVA's target.

Primary reference locations:

- Pi Bash tool: `docs/reference-code/pi/packages/coding-agent/src/core/tools/bash.ts`
- Shell and environment handling: `docs/reference-code/pi/packages/coding-agent/src/utils/shell.ts`
- Optional tool-call interception: `docs/reference-code/pi/packages/coding-agent/src/core/extensions/types.ts`

## Original AVA Problems Addressed

1. **Normal tools are hard-denied before approval.** Bare Python, Node, shell, Make, Ninja, package-manager, Bun, and Cargo frontends never reach the resolver.
2. **Classification is identity-inconsistent.** `python3 --version` is hard-denied while `/usr/bin/python3 --version` reaches an approval prompt because classification compares raw `argv[0]` instead of the resolved executable identity.
3. **Policy and execution parse independently.** `permission.cpp` and `bash_tool.cpp` can disagree about the command that was approved.
4. **PATH is not useful for normal development.** Local execution replaces PATH with fixed system directories and excludes common user and project toolchains.
5. **The remaining parent environment is inherited.** Provider, cloud, secret, and behavior-altering variables can reach approved children.
6. **Remembered rules match exact raw strings.** They are awkward when harmless arguments change, yet they do not express the actual executable, workspace, effect class, or environment.
7. **Frontends expose different scopes.** TUI offers once or persistent exact rules, RPC offers once/session grants, headless has no command-specific allow surface, and ACP executes in a different resolution domain.
8. **No OS authority boundary exists for ordinary project code.** A recognizable command such as `npm test`, CMake, Make, or pytest may execute arbitrary mutable project code.

Current AVA locations:

- Command classification: `src/ava/permissions/permission.{h,cpp}`
- Exact persistent rules: `src/ava/permissions/permission_rules.{h,cpp}`
- Local parsing and execution: `src/ava/tools/bash_tool.{h,cpp}`
- Delegated executor contract: `src/ava/tools/tool_io.h`
- Headless resolution: `src/ava/app/headless_policy.{h,cpp}`
- RPC resolution and grants: `src/ava/app/rpc/resolvers.{h,cpp}`
- TUI permission presentation: `src/ava/tui/composer_permission.cpp`, `src/ava/tui/runtime.cpp`
- ACP terminal delegation: `src/ava/app/acp/client_tools.cpp`

## Approved Three-Level Policy

### Level 1: Standard Development

Most commands in this level run without asking when AVA can recognize and contain them. Global or per-project user policy may change any default auto-allow to `ask` or `deny`.

Initial families include:

- bounded inspection and verification (`git status`, `git diff`, `git log`, `rg`, version queries);
- CMake configure/build and CTest;
- Ninja and Make build/test targets;
- Cargo build/check/test;
- npm, pnpm, Yarn, and Bun test or exact named project scripts;
- Python test runners and exact workspace scripts;
- Node exact workspace scripts;
- additional ecosystems only after each family has a bounded grammar and tests.

A project-code command is eligible for automatic execution only when all of these are true:

- AVA recognizes a typed command family and effect;
- the executable, arguments, workspace, and working directory form one validated command plan;
- all declared project paths stay within the workspace or separately approved roots;
- the command does not request network, privilege elevation, external state, raw shell evaluation, or an unbounded destructive effect;
- AVA supplies a secret-free child environment;
- the standard development containment profile is active.

If containment is unavailable, read-only inspection may remain automatic, but commands executing project code downgrade to `ask`. AVA must never claim that a build/test command is intrinsically harmless: remembered or automatic permission authorizes a constrained recipe, while containment limits what mutable project code can access.

### Level 2: Sensitive

Sensitive commands ask with a clear effect warning. The user may allow once, for the current session, or persist an eligible exact/typed rule globally or for one project.

Examples include:

- dependency installation or updates;
- Git push and other remote mutations;
- package publishing;
- deployment;
- network-enabled execution;
- deletion or broad mutation inside the workspace;
- commands with known external side effects.

AVA must show the exact command, executable, working directory, requested capabilities, and rule that would be remembered. Sensitive rules are never generated as broad executable-only wildcards.

### Level 3: Critical Or Unrestricted

Critical commands ask rather than being categorically forbidden. The default interactive prompt offers reject or run once, with an explicit statement that AVA cannot fully predict or contain the operation.

Examples include:

- privilege-changing commands such as `sudo`, `su`, or `doas`;
- system or device management;
- destructive access outside the workspace;
- raw shell evaluation, pipelines, redirects, substitutions, and command chains;
- inline interpreter programs such as `bash -c`, `python -c`, or `node -e`;
- unknown launch wrappers or commands whose execution identity cannot be represented by a standard recipe.

Normal UI flows do not offer a remembered critical grant. The command plan's maximum interactive approval scope is `once`. An advanced user may separately create an exact global or external per-project rule only with an explicit risk acknowledgement; that user-authored authority is validated before prompting and does not let a frontend widen a run-once reply. AVA never auto-generates a critical allow rule or a wildcard critical rule. Interactive password entry is not passed through the embedded tool runner; terminal handoff is deferred and commands that require it report an actionable unsupported-interactive-execution result.

## Command Representations

AVA will have two explicit execution lanes.

### Structured Command Lane

The preferred model/RPC representation uses exact arguments plus an optional workspace-contained working directory:

```json
{
  "argv": ["npm", "run", "test"],
  "cwd": "packages/frontend"
}
```

The existing command string remains a compatibility input, but it is parsed once into the same immutable plan. Empty arguments and argument boundaries must be preserved exactly.

### Raw Shell Lane

Shell syntax is supported as a critical/unrestricted operation rather than silently interpreted as a normal command. The complete shell text is displayed and authorized as one exact operation. It receives no reusable rule through the ordinary prompt. The raw-shell lane must not be described as statically safe merely because a parser lists its apparent subcommands.

## One Authoritative Command Plan

A new narrow command subsystem will produce one immutable plan before permission evaluation. Policy, prompting, audit, local execution, and delegated execution consume that same plan.

The plan includes:

- original safe display text;
- exact argv or exact raw-shell payload;
- canonical workspace and working-directory authority;
- resolved executable path, origin, and identity;
- supported interpreter/shebang chain where applicable;
- effective secret-free environment profile;
- semantic command family and effect class;
- requested filesystem/network/privilege capabilities;
- maximum approval scope;
- policy/environment schema versions;
- a stable fingerprint for grants, replies, and audit.

Execution must use the approved identity without falling back to a fresh PATH lookup. Executable and working-directory replacement between approval and launch must fail closed. Linux descriptor-anchored execution is preferred where it can correctly preserve supported interpreter chains; unsupported identity guarantees make the plan one-shot-only or unavailable rather than silently weaker.

## Global And Per-Project Policy

### Global User Policy

Proposed location:

```text
$XDG_CONFIG_HOME/ava/command-policy.json
```

It contains machine-wide defaults, typed rules, safe toolchain discovery paths, and user-selected policy overrides. It is user-owned, bounded, strictly validated, symlink-rejected, atomically written, and never model-writable.

### Effective Per-Project Policy

Proposed location:

```text
$XDG_CONFIG_HOME/ava/workspace-command-policies/<workspace-hash>/command-policy.json
```

The workspace hash uses AVA's existing normalized canonical-path hashing convention. Using XDG config is intentional because these are durable user policy choices, not transient runtime state. Moving the workspace changes its hash and requires fresh approval. The file stores user-approved exact plans or typed recipes outside the checkout so project code and model file tools cannot grant themselves authority.

### Checked-In Project Suggestions

Optional project file:

```text
<workspace>/.ava/command-policy.json
```

This file is loaded only after explicit project trust. It may propose recipes, working directories, or stricter denies, but it cannot grant execution authority by itself. AVA shows changes for user review and records the approved fingerprint externally. Any changed proposal invalidates that approval until reviewed again.

### Precedence

1. Invalid or stale plan/containment state fails closed.
2. Matching user deny rules override matching allows.
3. Global denies cannot be weakened by a project.
4. Project-specific user policy is more specific than global defaults where no deny applies.
5. Global user policy overrides AVA's built-in defaults where the command's maximum scope permits it.
6. AVA's built-in default applies when no user rule matches.

Project files never override external user authority.

## Typed Recipes

A recipe is a finite AVA-owned schema derived from a validated command plan, not a model-authored string wildcard. Examples:

- `CMakeBuild(workspace, build_directory)`
- `CTest(workspace, test_directory)`
- `PackageScript(workspace, manager, exact_script_name)`
- `CargoAction(workspace, build|check|test)`
- `WorkspaceScript(workspace, interpreter_family, exact_script_path)`

Recipes match structured fields, permitted argument classes, executable origin, workspace identity, working-directory policy, environment-policy version, and effect class. They never fall back to "first executable token plus any arguments." Existing deny rules are evaluated first.

The first rollout may support exact plan grants before reusable families. Each reusable family lands independently with adversarial matching tests. Approving a project-code recipe intentionally authorizes future mutable project code under that recipe; the UI must say this plainly, and the containment profile—not a content hash—is the damage boundary.

## Toolchain Discovery And Child Environment

AVA will derive command discovery from:

- a filtered snapshot of startup PATH;
- explicit global user toolchain directories;
- recognized workspace-local candidates such as activated virtual environments and `node_modules/.bin`.

Accepted discovery directories must resolve absolutely, have acceptable ownership and write permissions, and retain provenance. Empty/relative PATH entries and unsafe writable global directories are rejected. Workspace-local executables remain project-controlled and are classified accordingly. AVA does not source shell startup files.

Children receive a positive-list environment containing only documented compatibility values such as locale, user/home, temporary and XDG paths, the derived PATH, and explicitly approved non-secret toolchain variables. Provider, cloud, API, token, secret, arbitrary `AVA_*`, and loader/interpreter injection variables are excluded. Values are not persisted in permission audit records.

## Development Containment Profile

Permission decides whether a command may start; containment limits what approved mutable project code can do. Standard project-code auto-execution requires a verified containment profile.

Target behavior:

- system toolchains and required runtime libraries are readable/executable;
- the workspace and explicitly selected build/temp locations are writable;
- AVA auth, SSH keys, cloud credentials, unrelated home configuration, and external workspaces are unavailable;
- network is disabled unless the plan has separately approved network capability;
- restrictions are inherited by descendants, including descendants that create new sessions;
- AVA does not claim cgroup, namespace, or descendant containment without verified platform evidence.

The implementation plan must select and verify a Linux containment mechanism before automatic project-code execution is enabled. If the required containment guarantees are not available, the backend downgrades the command to an interactive permission prompt and reports why.

## Permission Experience

For an eligible standard/sensitive command, frontends may display:

- reject;
- allow once;
- allow this exact plan/recipe for the session;
- always allow this eligible recipe in this workspace.

Global rules are managed explicitly rather than casually created from a prompt. Critical commands normally expose reject/run-once only.

The prompt must show:

- exact command or argv;
- resolved executable and origin;
- working directory;
- effect/risk level;
- project-code, outside-workspace, network, privilege, and containment status;
- exact scope/recipe that a remembered choice creates.

The backend owns the maximum **interactive** approval scope and rejects scope escalation regardless of what TUI, RPC, ACP, or headless input requests. A manually authored external policy rule is a separate authority path: it must be eligible for the command level, exact when required, independently validated, and explicitly risk-acknowledged for critical commands. Config cannot make a frontend reply broader than the plan permits.

## Headless And ACP

Headless execution remains fail-closed for unresolved prompts. It gains process-local exact-plan approvals and, later, explicitly selected typed recipes. It never gains blanket `--allow-tool bash`. Critical raw-shell authorization is not inferred from a broad headless flag.

ACP executes in a client-controlled resolution domain. Reusable local recipes cannot apply until the ACP execution contract can carry and enforce the approved executable, cwd, environment profile, plan fingerprint, and capability bounds. Until then ACP command execution is once/session only or fails closed when equivalence cannot be established.

## Implemented Design Phases

The following phases were the approved planning inputs. The implementation uses these responsibilities with the concrete source layout described in the implementation record above.

### Phase 1: Identity Foundation

- Add the command-plan subsystem and one parser.
- Add structured argv and workspace-contained cwd while retaining compatibility inputs.
- Resolve executable/interpreter identities before policy.
- Route local and injected execution through the exact approved plan.
- Preserve current behavior behind a rollout switch while proving equivalence.

### Phase 2: Policy And Storage

- Add the three effect levels and backend-enforced maximum approval scope.
- Add global strict config and external workspace-keyed policy storage.
- Keep existing permission-rule schema behavior unchanged; never reinterpret old exact allows as broader recipes.
- Preserve legacy deny authority and prevent newly eligible build/test classes from being widened by old allows.
- Add explain/list/add/remove/revoke diagnostics.

### Phase 3: Toolchain And Environment

- Add filtered startup PATH and explicit toolchain discovery.
- Resolve system, user, and project-local executable provenance.
- Build a positive-list child environment.
- Bind environment/policy versions into the plan fingerprint.

### Phase 4: Containment

- Implement and verify the Linux standard development profile.
- Prove filesystem, secret, network, descendant, timeout, and teardown behavior.
- Keep automatic project-code execution disabled until containment evidence passes.

### Phase 5: Permission Defaults And Recipes

- Auto-allow recognized standard commands only under the approved preconditions.
- Convert normal development frontends from hard deny to structured standard/sensitive policy.
- Add exact plan grants, then typed recipe families one at a time.
- Add the critical raw-shell/run-once path.

### Phase 6: Frontend And Protocol Parity

- Align TUI, print, RPC, ACP, and direct command surfaces.
- Expose once/session/workspace choices only when backend-eligible.
- Add exact process-local headless approvals.
- Add visible rule explanations and revocation.

### Phase 7: Validation And Rollout

- Run focused, full, sanitizer, RPC/ACP, and real-toolchain smokes.
- Feature-gate rollout and preserve fail-closed rollback.
- Document the default command catalog, config schemas, containment guarantees, and unsupported platform behavior.
- Enable new defaults only after final review.

## Context Map

### Command And Containment Subsystems

| Path | Responsibility |
| --- | --- |
| `src/ava/command/command_plan.{h,cpp}` | Canonical parsing, argv/raw-shell representation, immutable plan and fingerprint |
| `src/ava/command/discovery.{h,cpp}` | PATH snapshot, executable/interpreter resolution and provenance |
| `src/ava/command/environment.{h,cpp}` | Positive-list child environment profiles |
| `src/ava/command/policy.{h,cpp}` | Three-level structural classification and maximum scope |
| `src/ava/command/recipes.{h,cpp}` | Exact grants and finite typed recipe matching |
| `src/ava/command/policy_store.{h,cpp}` | Global and external workspace policy validation/storage |
| `src/ava/command/containment.{h,cpp}` | Verified development containment profile and capability report |

The final implementation retains these responsibilities across `src/ava/command/`, `src/ava/containment/`, and the existing permission/tool/runtime boundaries; filenames differ where smaller existing seams were safer than the provisional layout.

### Existing Files Expected To Change

| Path | Planned role |
| --- | --- |
| `src/ava/permissions/permission.{h,cpp}` | Delegate command decisions to validated plans while retaining generic operation policy |
| `src/ava/permissions/permission_rules.{h,cpp}` | Preserve v1 compatibility and deny precedence; protect new stores |
| `src/ava/tools/bash_tool.{h,cpp}` | Compatibility adapter and exact-plan execution; no independent reparse/lookup |
| `src/ava/tools/tool_io.h` | Carry sealed execution specs to delegated executors |
| `src/ava/agent/tool_metadata.h` | Advertise structured argv/cwd and raw-shell behavior accurately |
| `src/ava/app/headless_policy.*` | Exact process-local command authorization |
| `src/ava/app/rpc/resolvers.*` | Plan fingerprints and backend-enforced scopes |
| `src/ava/app/acp/client_tools.cpp` | Capability-aware delegated execution or fail-closed behavior |
| `src/ava/tui/composer_permission.cpp` | Present backend-provided choices and recipe details |
| `src/ava/tui/runtime.cpp` | Submit typed choices; stop owning scope eligibility |
| `src/main.cpp` and CLI parsing owner | New command-policy/headless controls without growing unrelated main logic |
| `docs/CONFIG.md`, `docs/USAGE.md`, `docs/rpc-protocol.md` | Config, behavior, protocol, security and operational documentation |

### Proposed Test Areas

| Test area | Required evidence |
| --- | --- |
| Command-plan unit tests | Quoting/empty args, raw shell, control bytes, exact identity, fingerprint stability |
| Policy table tests | Standard/sensitive/critical families, absolute paths, wrappers, path/effect bounds |
| Recipe tests | No cross-subcommand, cross-script, cross-workspace, wildcard, or scope widening |
| Discovery tests | User toolchains, virtualenvs, project shims, symlinks, ownership, replacement races |
| Environment tests | Secret sentinels excluded; required safe/toolchain variables present |
| Containment tests | Workspace/system/secret/network access and descendant inheritance |
| Permission-store tests | Ownership, symlinks, corruption, locking, precedence, proposal fingerprint invalidation |
| Frontend tests | TUI/RPC/headless/ACP choices cannot exceed backend maximum scope |
| Process tests | Timeout, cancellation, TERM-to-KILL, spill limits, no detached survivors |
| Real smokes | CMake, CTest, Ninja/Make, Python/pytest, Node/package scripts, Cargo where installed |

## Acceptance Criteria

- Recognized contained standard development commands run without repetitive prompts.
- A user can change standard defaults globally or for one workspace.
- Sensitive commands prompt with clear effects and only create the scope the user selected.
- Critical commands prompt rather than being rejected by executable name; ordinary UI grants run once only.
- An explicitly risk-acknowledged exact critical rule can be manually stored outside the checkout.
- Bare and absolute executable paths receive the same classification.
- The exact approved executable, argv/raw shell, cwd, environment profile, and capability set are what execute.
- Project configuration cannot grant itself authority or modify effective external approvals.
- Matching denies always win and frontends cannot upgrade a plan's maximum scope.
- Safe auto-execution is disabled when required containment cannot be verified.
- Provider/cloud/API/token/secret values do not reach command children or permission logs.
- Existing timeout, cancellation, process-group cleanup, output caps, spill files, and audit behavior remain intact.
- ACP either proves execution equivalence or fails closed for unsupported reusable authorization.
- Existing v1 rules remain readable and rollback does not silently broaden authority.

## Rollback Strategy

- Schema-v2 typed recipes remain distinct from schema-v1 raw command Allows; v1 command Allows are read but non-authoritative.
- Do not migrate old command Allows into typed recipes automatically.
- On rollback, Standard auto-Allow returns to prompt/deny behavior; it never falls back to broader shell execution.
- Existing exact Denies remain authoritative during migration and rollback.

## Explicit Non-Goals

- Copying OpenCode's default `"*": "allow"` for all shell commands.
- Copying Pi's unrestricted default Bash authority.
- Treating command-name recognition as proof that project code is harmless.
- Letting checked-in project files, plugins, MCP servers, or model output grant command authority.
- Adding model-authored regex/glob permission patterns.
- Claiming sandbox, cgroup, network, or descendant containment without verified evidence.
