# AVA Frontend Roadmap

**Status: F1 in progress — quiet composer shipped — 2026-07-20**

This is a post-MVP product-maturity roadmap for AVA's terminal frontend. It
sets the sequence, evidence, and scope boundaries for making AVA feel more
visually calm, responsive, and predictable in daily coding work, with
OpenCode as a behavior and quality reference.

OpenCode is never an architecture or source to copy. AVA remains a native
C++23/ncursesw application with narrow semantic frontend/backend seams. The
work described here should adapt useful user-visible behavior to AVA's
existing runtime and rendering boundaries, not adopt OpenTUI, Solid, web
components, or reference implementation details.

## 1. Product Goal

### Frontend maturity goal

AVA should present a terminal coding session with:

- clear visual hierarchy, so the active work, current decision, and next
  available action are apparent without reading every line;
- responsive layout, so wide, ordinary, and narrow terminals remain useful
  rather than merely fitting the same dense layout into fewer columns;
- predictable interaction, with stable keyboard, mouse, modal, draft, and
  transcript behavior;
- progressive disclosure, keeping routine messages compact while making tool,
  permission, diagnostic, session, and diff detail available on demand; and
- evidence, using deterministic renderer coverage, controlled PTY captures,
  and opt-in terminal-protocol smokes rather than aesthetic claims alone.

The goal is a mature AVA-native frontend, not visual or feature-count parity
with every OpenCode surface. Matching a behavior is useful only when it makes
coding sessions clearer, safer, or faster in AVA.

### Relationship to existing plans

`docs/roadmap/backend.md` owns backend capability, durability, provider,
runtime-event, and automation sequencing. This roadmap consumes the semantic
contracts that backend work exposes; it does not prescribe renderer details to
the backend or turn frontend presentation into a backend responsibility.

The Pi-first MVP TUI/editor/terminal goal is already documented as closed in
`docs/goals/pi-mvp-parity/tui-editor-terminal.md`, with the living baseline in
`docs/product/mvp-baseline.md`. This plan starts after that baseline. It must
not describe core editor, markdown, tool-card, permission, session, image,
or terminal features as if they were absent. Instead, it organizes the next
maturity pass: hierarchy, density, responsive behavior, discovery, and
well-bounded polish.

## 2. Scope and source map

### AVA frontend surface

| Area | Primary AVA paths | Planning focus |
| --- | --- | --- |
| Runtime and event routing | `src/ava/tui/runtime.{h,cpp}` | semantic-event consumption, redraw ownership, layout coordination |
| Composer and editor | `src/ava/tui/composer.{h,cpp}`, `composer_internal.h`, `composer_editor.*`, `composer_input.cpp` | draft stability, input geometry, completion, command discovery |
| Overlays and selections | `composer_palette.cpp`, `composer_select_list.cpp`, `composer_permission.cpp` | hierarchy, geometry, narrow layouts, keyboard/mouse clarity |
| Transcript and text | `composer_transcript.cpp`, `composer_text.cpp`, `text.*`, `text_wrap.*` | message grouping, reading rhythm, visible-window performance |
| Tools and diffs | `tool_cards.*`, `composer_diff.cpp` | compact summaries, detail disclosure, result and denial clarity |
| Terminal/platform | `terminal.*`, `terminal_image.*`, `event_state.*` | resize, mouse, images, links, cleanup, capability fallbacks |
| Interaction system | `keybindings.*`, `theme.*` | preserved controls, discoverability, visual tokens, theme limits |
| Application integration | `src/ava/app/line_shell.cpp`, `command_palette.*`, `display_settings.*`, `interactive_run_queue.*`, `events.*`, `onboarding.*`, `clipboard_image.*`, `reasoning_controls.*`, `runtime_sessions.*` | semantic boundaries, application state, settings, session integration |
| Tests | `tests/tui_composer_tests.cpp`, `tests/tui_tmux_smoke.py`, `tests/tui_smoke_helpers.py`, `tests/tui_kitty_image_smoke.py`, `tests/tui_osc8_smoke.py`, `tests/CMakeLists.txt` | deterministic behavior and terminal evidence |

Large runtime, transcript, and keybinding orchestration files are known
pressure points, but their size alone does not justify a broad rewrite. Split
only at a demonstrated ownership or testability boundary.

### OpenCode reference snapshot

The local behavior reference is `docs/reference-code/opencode/`, snapshot
commit `7a8e7c8` dated 2026-07-04, with `packages/tui` version `1.17.13`.
It is useful for comparing user-visible quality, not for copying source,
architecture, data flow, or implementation details.

| Reference area | OpenCode paths to compare behaviorally |
| --- | --- |
| App and shell | `packages/tui/src/app.tsx`, `routes/home.tsx`, `routes/session/index.tsx`, `routes/session/sidebar.tsx`, `component/startup-loading.tsx` |
| Transcript | `routes/session/index.tsx`, `routes/session/dialog-message.tsx`, `util/transcript.ts`, `util/layout.ts` |
| Prompt | `component/prompt/index.tsx`, `component/prompt/autocomplete.tsx`, `prompt/history.tsx`, `prompt/traits.ts`, `editor.ts`, `config/keybind.ts` |
| Tools and prompts | `routes/session/index.tsx`, `routes/session/permission.tsx`, `routes/session/question.tsx` |
| Overlays and navigation | `ui/dialog.tsx`, `ui/dialog-select.tsx`, `component/command-palette.tsx`, `component/dialog-session-list.tsx`, `routes/session/dialog-timeline.tsx`, `routes/session/dialog-fork-from-timeline.tsx`, `keymap.tsx` |
| Platform and polish | `clipboard.ts`, `util/selection.ts`, `component/prompt/local-attachment.ts`, `ui/link.tsx`, `attention.ts`, `feature-plugins/system/notifications.ts`, `ui/toast.tsx`, `component/dialog-status.tsx`, `feature-plugins/system/diff-viewer.tsx`, `feature-plugins/system/which-key.tsx`, `feature-plugins/sidebar/`, `theme/index.ts`, `context/theme.tsx` |
| Tests | `packages/tui/test/app-lifecycle.test.tsx`, `keymap.test.tsx`, `config.test.tsx`, `cli/tui/prompt-submit-race.test.ts`, `prompt/`, `clipboard.test.ts`, `theme.test.ts`, `cli/cmd/tui/notifications.test.ts`, `cli/tui/diff-viewer*.test.tsx`, `util/transcript.test.ts` |

## 3. Truthful current baseline

AVA already has a strong terminal frontend foundation:

- a native ncursesw runtime with a multiline composer, selection, undo/redo,
  completion, slash palette, draft preservation, mouse input, and configurable
  keybindings;
- transcript rendering for assistant text, markdown, reasoning, tool lifecycle
  updates, diffs, changed paths, bounded output, cancellation, and errors;
- permission and question flows, model/settings/scoped-model/session/tree and
  other selectors, session navigation, onboarding, and active-run queue
  feedback;
- light, dark, plain, and local custom theme support, plus terminal capability
  handling for resize, paste, keyboard protocols, mouse, OSC 8/52, and
  Kitty/iTerm2 image paths with textual fallbacks; and
- deterministic TUI composer tests plus opt-in tmux, Kitty image, and OSC 8
  PTY smokes.

Known residual limits are equally important:

- AVA deliberately has no deterministic terminal screen model. Renderer tests
  plus controlled PTY smokes are the accepted MVP strategy; this roadmap must
  make the resulting evidence clearer before proposing another renderer.
- Image sizing uses a fixed 9x18 terminal-cell pixel fallback. That is an
  explicit platform-quality limitation, not a claim of pixel-accurate image
  layout.
- Keyboard-only and plain/no-color behavior have MVP coverage, but a broader
  accessibility and screen-reader audit is deferred.
- Rendering is bounded in important paths, but profiling against broader
  real-workload transcripts remains incomplete.
- Deeper diff navigation, per-tool affordances, non-tool denial polish, and
  theme breadth remain product-polish work.

### Baseline visual audit

Existing tmux captures provide useful, but viewport-specific, audit evidence.
They suggest that wide-terminal sessions can retain a permanent sidebar,
show verbose or repeated tool detail, render dense selector rows or raw
identifiers, and truncate modal or prompt hints at narrower widths. Startup
and authentication diagnostics can also be more verbose than the routine
first action needs. These are observations to validate across the baseline
matrix, not assertions that every viewport or session is already defective.

### Immediate P0 regression — ordinary Space completion closed in F0

**Trigger:** type a normal word, then press Space. **Closed behavior:** the
space remains one normal trailing draft cell and no file, path, or reference
menu opens. Passive completion still opens from a real `@` prefix or naturally
path-like token, and Tab still explicitly forces empty-token suggestions.

This was normal path completion, not literal `@` completion. The trigger-policy
seam in `src/ava/tui/composer_palette.cpp::find_path_completion_prefix()`
accepted an empty token after whitespace as a non-forced prefix, so its empty
query matched every workspace candidate. The narrow fix makes the
`start >= input.size()` branch return `std::nullopt` for non-forced completion
while preserving its empty prefix for forced completion. Space insertion,
runtime handling, keybindings, literal `@` parsing, fuzzy scoring, and
path-like token rules are unchanged.

The deterministic path-completion section now proves that non-forced
`inspect ` has no matches, keeps its palette hidden, and leaves selection text
and cursor unchanged; it also proves forced completion after that whitespace
still offers the workspace candidates. Existing positive real-`@`, `src/`,
equals/quoted, forced bare-token, and forced slash-argument checks remain.
The isolated Python real-TTY scenario types `ordinary`, observes the Space
cursor move from `13,29` to `14,29`, checks workspace palette rows remain
absent for a bounded interval, verifies the styled draft, saves evidence, and
then proves Tab opens forced suggestions.

The ordinary-Space gate remains part of the completed F0 record. The semantic
state inventory, character-cell matrix, named evidence catalog, and capture
policy are now recorded in
[`frontend-evidence-baseline.md`](frontend-evidence-baseline.md). The first
bounded F1 composer checkpoint is recorded below; the historical F0 evidence
remains unchanged.

## 4. Product principles and preserved controls

1. **Semantic seams first.** The frontend consumes semantic backend events and
   established application state. It must not require renderer-specific backend
   events, reconstruct paths or sessions from presentation text, or infer
   missing state from JSONL/session paths.
2. **One primary reading order.** A turn should lead from user intent to
   assistant outcome to any action requiring attention. Repeated summaries
   should collapse before primary content does.
3. **Responsive means changing policy.** Narrow terminals may hide or move
   secondary chrome, shorten labels, and alter overlay geometry. They must not
   silently lose critical state or controls.
4. **Details are earned.** Tool payloads, full IDs, diagnostics, diffs, and
   metadata remain available through clear expansion, copy, inspect, or
   navigation paths rather than occupying every resting screen.
5. **Input is sovereign.** Transcript navigation, overlays, and active-run
   state must not unexpectedly overwrite or relocate a draft.
6. **State is not color-only.** Plain/no-color and keyboard paths preserve
   permission, error, selection, running, and completion meaning.
7. **Change with evidence.** Each layout policy needs fixtures and captures at
   representative cell sizes, plus cleanup checks for terminal state and
   control sequences where a real PTY is involved.
8. **Use the smallest native change.** Do not introduce a broad reusable
   component rewrite or heavy terminal dependency without explicit approval.

### OpenCode-inspired terminal design direction

“Minimalistic yet premium” means restraint, alignment, predictable hierarchy,
crisp spacing, and fast interaction—not more borders, badges, colors,
animations, or metadata. Establish a neutral visual foundation with one
restrained accent; every state must still be textual and meaningful in
plain/no-color terminals. Avoid box or card nesting: use whitespace, small
gutters, and separators only when they communicate ownership.

Each screen has one clear focus target and one obvious reading order. Critical
states outrank decorative or status metadata. The same rules must make
idle, loading, streaming, success, error, canceled, and permission states
unambiguous; the treatment may be compact, but it must not rely on color or
ornament to explain what happened or what needs attention.

At idle, the composer has one quiet input surface and at most one visual
boundary or gutter: no nested frame/card, duplicate composer title, persistent
help row, empty attachment/queue/status row, or open menu. Only the
draft/placeholder and settled one-line footer persist; that footer may show the
active model, known `ctx N`, and the active spinner only. Contextual rows appear
only while populated or relevant and release their rows when empty. These are
terminal-native layout rules, not a pixel-look requirement.

### Settled AVA differences

The following are preserved product decisions, not candidates for accidental
OpenCode-style convergence:

1. Plain **Up/Down** scroll transcript history only. They never move or
   replace the composer draft. History and cursor-vertical actions remain
   configurable but are unbound by default.
2. The composer footer remains minimal: active model, known `ctx N`, and the
   active spinner while processing. Do not reintroduce cwd, git, AVA branding,
   mode, provider, session metadata, token usage, or reasoning metadata there.
3. The frontend consumes semantic backend events. It does not create
   renderer-specific backend contracts or reconstruct path/session state.
4. No broad reusable-component rewrite or heavy terminal dependency is in
   scope without approval.

## 5. Priority categories

### P0 — baseline alignment

P0 is the product-maturity baseline. It covers responsive shell/sidebar policy;
transcript and message hierarchy; the composer and command discovery; command
palette/selectors; compact tool and diff cards; permission/question flows; and
session/navigation overlays. Work is accepted only when it improves the
resting screen and narrow behavior without regressing settled controls.

### P1 — platform polish

P1 covers the in-TUI presentation of status already exposed by AVA, plus
mouse/clipboard/images/links, theme and accessibility refinement, terminal
cleanup, and workload-oriented performance evidence. It does not authorize a
new desktop, audio, or terminal notification surface; all such notification
work remains P2/F7. P1 should make already-present functionality more
dependable across terminal environments, not add new product surfaces by
default.

### P2 — optional or later decisions

P2 holds optional notification, terminal-title, standalone diff-viewer,
Which-Key, and broader theme decisions. It also holds any OpenCode-inspired
surface that needs backend capability work, such as session revert/redo,
background-job control, plugin UI, or a full diff viewer. P2 work starts only
after an explicit product decision and stable semantic contracts where needed.

Matching behavior does not mean adopting all OpenCode surfaces. AVA may choose
a smaller, safer path if it preserves the intended task outcome.

## 6. Gap matrix

| Area | Desired behavior | Current AVA state | Closure work | Acceptance / evidence | Backend dependency |
| --- | --- | --- | --- | --- | --- |
| Shell/sidebar | Secondary navigation yields space when terminals narrow; stable context remains reachable | Rich sidebar/session context exists; wide captures suggest it can be permanently visible | Define cell-width policy, collapse/reveal affordance, and focus/selection continuity | Baseline matrix captures at wide, ordinary, and narrow sizes; no hidden critical state | Existing session/model semantic state; do not infer from paths |
| Transcript | User, assistant, reasoning, tool, and error blocks have a calm, scannable hierarchy | Rich markdown and cards are present; dense visual grouping remains possible | Establish spacing, headings, compact states, and expansion hierarchy | Renderer fixtures and tmux capture review show one clear reading order | Existing message/tool lifecycle events |
| Composer | One quiet input surface with minimal chrome/footer; discovery is intentional and never competes with a draft | Mature multiline editor and minimal footer already exist | Preserve footer/input and transcript-only Up/Down behavior; allow passive menus only for `/`, real `@`, or path-like input, while explicit completion may force suggestions; keep contextual hints relevant and non-competing; anchor menus consistently without obscuring cursor/draft or losing focus on resize/cancel; keep attachments, queued work, and status above input and collapse them when absent | At each baseline width, focused editor renderer/capture assertions prove stable trailing spaces, cursor, and draft; no accidental popup or draft jump; preserved footer; and absence of duplicate/nested chrome and idle menu/status rows | None beyond existing commands/model context |
| Command palette | Commands and arguments are discoverable without raw-ID overload | Fuzzy palette and command integrations exist | Normalize labels, descriptions, disabled reasons, and argument states | Keyboard/mouse selection evidence at narrow widths | Command metadata and availability reasons where exposed |
| Selectors | Lists are readable, selected rows visible, and actions obvious at small heights | Many modal/select-list workflows exist; capture evidence suggests dense/raw rows | Shared row-window and label policy; preserve exact command selectors | Deterministic selection fixtures plus tmux evidence | Existing model/session/settings data |
| Tools | Resting transcript shows concise action/outcome; deep payload remains available | Lifecycle, output, diff, spill, and permission details are rich and may duplicate | F2 establishes only transcript placement/grouping/rhythm and a generic compact card shell; F5 owns status/target/outcome/duration anatomy, lifecycle and safety states including `permission required / awaiting decision`, progressive detail, specialized previews, inputs/output/IDs, permission linkage, copy, paths, diffs, truncation/spill, and detailed evidence | F2 evidence covers generic shell placement, spacing, one primary summary line, collapsed/expanded placement, and no adjacent duplicate summary; F5 compact/expanded renderer/PTY evidence at 160x48, 120x36, 80x24, and short height covers detailed states, reachable/copyable expansion, and actionable failures/permission decisions | Tool event summaries, changed paths, status, bounded payloads |
| Diffs | Changed paths and a useful compact diff entry point are visible without a full viewer | Inline diff rendering exists; deeper navigation is deferred | Improve card hierarchy only; gate standalone navigation/viewer | Focused card fixtures; no claim of full viewer | Stable diff/path metadata; full viewer requires a contract decision |
| Permissions/questions | Prompt risk, reason, choices, and outcome are easy to understand in narrow/plain layouts | Structured flows and remembered rules exist | Normalize geometry; treat `permission required / awaiting decision` as a safety-critical state distinct from queued/pending/running; narrow choices without hiding allow/deny meaning | Deterministic narrow/plain and Python tmux evidence cover the permission-required prompt/tool state, its reason/request identity and allow/reject follow-up, plus choices, denial, and result card | Existing permission/question request and resolution events |
| Sessions/navigation | Switching, tree navigation, and session context are discoverable without permanent density | Session/tree contracts and selectors are present | Responsive sidebar and session-overlay information hierarchy | Session selector captures with keyboard and mouse continuity | Existing session-tree/name/label state; no pathname reconstruction |
| Status/attention | Existing completion, failure, queued, and attention states are visible without noisy persistent chrome | Startup, active-run, and auth diagnostics exist | Refine quiet in-TUI status treatment; keep every new notification surface behind F7 approval | Captures distinguish transient from persistent status | Event severity/terminal state if already exposed; otherwise backend work |
| Mouse/clipboard/images/links | Capability-enhanced paths degrade to useful text and clean up terminal state | Mouse, OSC 8/52, image fallback/emission support exists; image fallback size is fixed | Verify fallback, selection, link, image-row, and cleanup behavior | Kitty/OSC8 opt-ins, PTY cleanup scan, manual pixel supplement | Terminal capability data only; no backend presentation contract |
| Themes/accessibility | Meaning survives plain mode and themes remain coherent | Light/dark/plain/custom themes and keyboard paths exist | Audit contrast/text affordances, names, and narrow plain layout; defer screen-reader breadth | Deterministic plain/theme cases and documented manual audit | None |
| Performance/testing | Layout behavior is repeatable and real terminals leave no state behind | Renderer tests plus smokes are accepted MVP strategy; workload profiling incomplete | Matrix fixtures, capture naming, bounded profile scenarios, terminal cleanup checks; require the Python real-TTY harness for interactive behavior | Focused C++ suite, closest isolated Python tmux scenario, pane-capture inspection, control-sequence and cleanup checks | None |

## 7. Dependency gates

Frontend code must not guess missing semantic data. If a desired label, state,
action, ownership, risk reason, diff identity, session relation, or job state
is not exposed by a stable semantic backend contract, record it as backend
work and preserve a truthful fallback. Summary-only events need an AVA-native
frontend fallback that remains readable, rather than fabricated detail.

The following remain gated unless a stable backend contract already exists:

| Capability | Gate |
| --- | --- |
| Session revert/redo | Explicit reversible-session semantics, conflicts, permission/audit meaning, and terminal event state |
| Background-job control | Job status, wait/result/cancel ownership, lifecycle events, and retained-result contract |
| Plugin UI | Versioned extension UI request/response boundary with permission, trust, cancellation, and diagnostics semantics |
| Full diff viewer | Stable diff identity, path/content availability, bounded retrieval, navigation, and permission semantics |

A frontend may render a stable existing contract, but must neither add a
renderer-specific contract nor treat a temporary payload shape as one.

## 8. Milestones

### F0 — contract and evidence baseline

**Status: Complete — 2026-07-19**

**Purpose:** established a shared, reproducible starting point before changing
layout or interaction policy. The authoritative inventory, measured
character-cell matrix, evidence catalog, capture policy, and tracked future
gaps are in [`frontend-evidence-baseline.md`](frontend-evidence-baseline.md).

**Completed scope:**

- Recorded semantic event and application-state seams consumed by the TUI for
  transcript, tool, permission, question, session, model, queue, diagnostics,
  and terminal-capability presentation without inventing backend data.
- Captured the current idle/onboarding shell at 160x48, 120x36, 80x24, and
  100x12, and catalogued the existing plain/no-color, active-run, permission,
  selector, tool, diff, session, paste, resize, attachment, and protocol
  evidence.
- Recorded the accepted renderer-test plus PTY-smoke strategy, including the
  intentional absence of a deterministic terminal screen model.
- Defined evidence ownership, capture normalization, control-byte inspection,
  and terminal-state cleanup expectations.
- Retained the closed ordinary-Space path-completion regression as a tested F0
  gate before visual redesign.

**Closure criteria met:**

- Every P0 state has a named deterministic or real-TTY/protocol evidence target
  and a semantic source or named future gap.
- The matrix separates observed current behavior from future breakpoint policy.
- Saved text captures are plain, text-safe, and checked for unexpected control
  bytes.
- The ordinary-Space regression has deterministic C++ and isolated Python tmux
  evidence: non-forced `ordinary ` is hidden, trailing-space cursor movement is
  stable, and `@sr`, `src/`, and forced Tab still complete. The tmux scenario
  saves `composer-ordinary-space-no-completion.txt` after its cursor/no-palette
  assertions.
- F0 ships no visual redesign.

#### F0 implementation checkpoint — ordinary Space regression

The regression was reproduced red before the production change. The focused
C++ run failed the expected no-match, hidden-palette, and unchanged-selection
assertions; the unfixed tmux run failed because the workspace palette appeared
after Space. After the narrow prefix-policy fix, these commands passed:

```sh
scripts/build.sh --build-dir build --target ava ava_tests ava_fake_provider_server
scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tests\.tui_composer$'
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tui\.tmux_smoke_main_slash_completions$'
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 13 -R '^ava_tui\.tmux_smoke_'
```

The isolated regression scenario and the complete 13-scenario tmux wave passed;
the full wave completed 13/13 with no failed terminal scenario. The persisted
artifact was found at
`build/tui-tmux-smoke/main_slash_completions/evidence/composer-ordinary-space-no-completion.txt`.
It contains the visible `ordinary` draft and no `src/main.cpp`, `src/`, ESC,
or other C0 control bytes except LF. The scenario observed cursor `13,29` to
`14,29`, completed its forced-Tab continuation, exited its TUI, and cleaned up
its private tmux/provider resources. The ordinary-Space checkpoint is retained as a completed F0 gate; the broader
F0 inventory and evidence closure are recorded below and in
[`frontend-evidence-baseline.md`](frontend-evidence-baseline.md).

#### F0 closure checkpoint — frontend evidence baseline

The completed F0 record is
[`frontend-evidence-baseline.md`](frontend-evidence-baseline.md). The following
commands passed against the current configured `build/` tree:

```sh
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tui\.tmux_smoke_main_startup_trust_keybinds$'
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 13 -R '^ava_tui\.tmux_smoke_'
```

The targeted baseline scenario passed, and the isolated full wave passed 13/13.
It saved and inspected these plain-text artifacts:

- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-wide-idle-shell.txt`
- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-ordinary-idle-shell.txt`
- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-narrow-idle-shell.txt`
- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-short-idle-shell.txt`

The scenario asserts exact tmux dimensions at 160x48, 120x36, 80x24, and
100x12; it synchronizes the target-specific composer/footer reflow at
zero-based rows `height - 3`/`height - 2`, asserts the current sidebar at
wide/ordinary widths and its absence at narrow/short widths, then restores
120x32 and asserts rows 29/30. Inspection confirmed the composer/footer in
every artifact, no ESC or unexpected C0 bytes, and cleanup of private
tmux/fake-provider resources. This is evidence closure only; no F1 visual
change is claimed.

### F1 — visual system and responsive shell

**Purpose:** give AVA a consistent visual language and responsive shell policy.

**Scope:**

- Define visual tokens and hierarchy using existing theme/runtime seams.
- Establish wide, ordinary, and narrow sidebar behavior, including discoverable
  reveal/navigation and focus continuity.
- Normalize outer gutters, section spacing, divider use, status placement, and
  header/footer priority without expanding the composer footer.
- Preserve the minimal model/known-context/spinner footer and existing input
  behavior.

**Acceptance criteria:**

- The same active state is understandable at every matrix width.
- Narrow policy never hides a permission, active run, selected modal choice,
  or draft without an accessible path to it.
- At every baseline width, renderer/capture assertions prove the idle composer
  has no duplicate/nested chrome or idle menu/status rows, while its draft and
  cursor remain stable.
- Footer assertions prove only active model, known `ctx N`, and an active
  spinner while processing appear.
- Existing Up/Down draft behavior remains unchanged.

#### Bounded F1 checkpoint — quiet composer shipped

This checkpoint ships only the quiet composer slice; it does not complete F1.
The idle and single-line composer now occupies exactly the bottom two rows:
input, then footer, with no blank composer-surface padding row. Every visible
input, continuation, and footer row starts with the same three-cell `│  `
semantic boundary and gutter, and the former `❯` prompt glyph is absent.
Multiline height is the visible wrapped input-line count plus one footer,
clamped to 2..8 rows. The existing composer surface color and footer contract
remain unchanged: active model, known `ctx N`, and the active spinner only.

Sidebar policy is deliberately unchanged in this slice. It still requires
semantic sidebar data and width >=112 and remains bounded to 38 columns.
Responsive sidebar disclosure, breakpoint/reveal behavior, focus continuity,
and the remaining F1 visual-system/shell hierarchy work are still open. No
tool-card or sidebar redesign began here.

Test-first evidence was captured against the old implementation. The focused
C++ test ran red with 27 expected composer failures; representative failures
were `tui keeps a one-line draft in exactly the bottom input and footer rows
without composer-surface padding` and `tui composer input and continuation
rows share one three-column boundary and gutter`. The updated real-TTY matrix
also ran red against the old binary by timing out waiting for F1 rows 46/47 at
160x48 while the captured frame still showed `▎  ❯` at rows 45/46.

After the production change, these validations passed:

```sh
scripts/build.sh --build-dir build --target ava ava_tests ava_fake_provider_server
scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tests\.tui_composer$'
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tui\.tmux_smoke_main_startup_trust_keybinds$'
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 13 -R '^ava_tui\.tmux_smoke_'
python3 -m py_compile tests/tui_tmux_smoke.py
```

The targeted scenario passed and the isolated tmux wave passed 13/13. Current
plain-text captures are:

- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-f1-wide-idle-composer.txt`
- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-f1-ordinary-idle-composer.txt`
- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-f1-narrow-idle-composer.txt`
- `build/tui-tmux-smoke/main_startup_trust_keybinds/evidence/frontend-f1-short-idle-composer.txt`

Inspection confirmed exact dimensions and zero-based input/footer rows 46/47,
34/35, 22/23, and 10/11; `│  ` prefixes; no `❯`; footer text exactly
`GPT-5.5 · ctx 1` after removing the leading gutter and wide sidebar separator;
the historical sidebar policy visible/visible/absent/absent; no ESC or
unexpected C0 bytes; final LF; and cleanup of private tmux/provider resources.
The ordinary-Space scenario and all other tmux interaction scenarios remained
green. `git diff --check` and explicit no-index whitespace checks for both new
roadmap documents also passed.

The F1 review found a task-caused packaged-documentation link mistake:
`docs/TESTING.md` linked to the historical baseline roadmap document, but
roadmap documents are not in the package payload. This was not an
untracked/index condition, and committing would not correct it. The link is
now an inline source reference to
`docs/roadmap/frontend-evidence-baseline.md`, so the packaged documentation has
no local link to an omitted roadmap file.

The review ledger is closed: `F1-QC-001` fixed the packaged-documentation
link, and `F1-QC-002` restored the too-narrow Markdown fallback assertion so
it rejects table-border glyphs on transcript rows while exempting only the
exact quiet-composer rows.

After the review fixes, formatting `tests/tui_composer_tests.cpp` with the
repository `.clang-format`,
`scripts/build.sh --build-dir build --target ava_tests`,
`scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tests\.tui_composer$'`,
and
`scripts/run-tests.sh --build-dir build --jobs 1 --output-on-failure -R '^ava_release\.package_linux$'`
all passed. The full default wrapper ran 99 CTest entries: the package test
passed, and the only two failures were the known unrelated dirty-backend cases
`ava_tests.acp` (missing MCP subprocess/exit 127 rather than the expected
registry error) and `ava_cli.headless_rpc_plugin_commands` (the newer sanitized
plugin failure rather than its older raw phrase).

### F2 — transcript and message hierarchy

**Purpose:** make long conversations easier to scan while keeping details
available.

**Scope:**

- Refine transcript placement, grouping, spacing, and reading rhythm for user
  prompts, assistant answers, reasoning, tools, errors, queue state, and
  summary-only events.
- Define only the generic compact tool-card shell: where it appears, its
  spacing, one primary summary line, generic collapsed/expanded placement, and
  no adjacent duplicate summary.
- Do not define per-state lifecycle anatomy, specialized previews, inputs, full
  output, IDs, permission audit, diff details, permission linkage, or the full
  evidence matrix here; F5 exclusively owns that detailed tool UX.
- Preserve markdown and bounded transcript-rendering behavior.

**Acceptance criteria:**

- A representative multi-turn capture has one obvious reading order.
- Generic compact cards have stable placement and spacing, one primary summary
  line, predictable collapsed/expanded placement, and no adjacent duplicate
  summary.
- Summary-only backend events display a truthful useful fallback.
- Large transcript cases remain bounded by existing or added focused evidence.

### F3 — composer and command discovery

**Purpose:** keep the composer calm and make available actions discoverable.

**Scope:**

- Treat the composer as one quiet input surface with at most one boundary or
  gutter and the settled minimal footer: no nested frame/card, duplicate title,
  persistent help row, empty attachment/queue/status row, or idle open menu.
  Only the draft/placeholder and one-line footer persist at rest; contextual
  rows appear only while populated or relevant and release their rows when
  empty. Improve command palette, autocomplete, relevant inline hints,
  unavailable states, and overlap behavior at narrow widths.
- Make `/`, a real `@` prefix, path-like input, or an explicit completion
  action intentional discovery triggers; normal typing and ordinary Space
  never open a passive menu.
- Keep contextual hints from competing with the draft; keep attachments,
  queued work, and status above the input and collapse them when absent.
- Anchor menus consistently to the composer without obscuring the current
  cursor/draft, and preserve focus through resize and cancel.
- Normalize command and model/session-facing labels without requiring raw IDs
  as the primary visible text.
- Improve discovery of active-run follow-up, queue, restore, and cancellation
  behavior using existing semantics.
- Preserve draft ownership, configured custom keys, and default arrow policy.

**Acceptance criteria:**

- A draft is unchanged by plain Up/Down transcript scrolling.
- Palette and completion fixtures show visible selection, useful descriptions,
  and no essential hint truncation at supported narrow widths.
- At every baseline width, renderer/capture assertions prove no duplicate or
  nested chrome and no idle menu/status row, while footer and normal input
  semantics remain unchanged, including stable trailing spaces, cursor, and
  draft with no accidental popup or draft jump.
- Exact selector commands continue to open their intended selector behavior.

### F4 — overlays, selectors, and session navigation

**Purpose:** make modal workflows predictable and legible across terminal
sizes.

**Scope:**

- Apply a consistent overlay geometry, title, prompt, list, action, and help
  policy to command, model, settings, session/tree, permission, and question
  views.
- Keep selected rows visible and align rendering with mouse hit testing.
- Shorten or disclose secondary identifiers rather than crowding primary labels.
- Make responsive sidebar and session navigation work together without
  reconstructing session state from path text.

**Acceptance criteria:**

- Eight-to-twelve-row terminal fixtures keep selected rows and required actions
  visible.
- Keyboard-only and mouse selection choose the same semantic item.
- Plain/no-color captures preserve disabled, selected, and risk meaning.
- Session navigation uses existing stable IDs/state, not renderer parsing.

### F5 — tools, diffs, permissions, and questions

**Purpose:** reduce repeated operational detail while improving safety-critical
clarity.

**Scope:**

- Exclusively define detailed tool UX beyond F2's generic shell: status,
  target, outcome, and duration anatomy; every lifecycle and safety state;
  progressive detail; specialized preview rules; inputs, full output, IDs,
  permission audit/linkage, copy behavior, changed paths, diffs, and
  truncation/spill treatment.
- Give queued/pending, running, success, failed, canceled, denied,
  `permission required / awaiting decision`, truncated/spill, changed-path,
  and diff states distinct textual, never color-only treatment. The
  permission-required state is safety-critical and distinct from
  queued/pending/running: its compact treatment visibly says permission is
  required, shows risk, and points to the available decision action without
  color dependence.
- In the permission prompt and expanded tool detail, keep the reason and
  request identity visible with allow/reject choices and any follow-up. Avoid
  duplicate model-facing result text while allowing specialized previews under
  these detailed rules.
- Improve changed-path/diff entry points without implementing a full diff viewer.
- Normalize permission and question geometry, remembered choices, denial
  follow-ups, and narrow choice labels; improve non-tool denial presentation
  where current semantic data supports it.

**Acceptance criteria:**

- Queued/pending, running, success, failure, cancellation, truncation/spill,
  diff, permission-required/awaiting-decision, permission allow, permission
  deny, and question fixtures each expose the required next action.
- Default resting cards do not repeat equivalent result content.
- Expanded cards are keyboard/mouse reachable and copyable; failures and
  denials never hide their next action. Permission-required prompts/details
  retain reason and request identity with allow/reject choices and follow-up.
- The detailed evidence matrix covers compact and expanded states at 160x48,
  120x36, 80x24, and short-height terminals. Deterministic narrow/plain and
  Python tmux evidence specifically cover the permission-required prompt/tool
  state, not merely allowed/denied outcomes.
- Any missing field is tracked as backend work rather than synthesized.

### F6 — terminal, platform, accessibility, and performance

**Purpose:** harden the mature layout in real terminal environments.

**Scope:**

- Exercise resize, tmux, mouse, clipboard, OSC 8, OSC 52, Kitty/iTerm2 image
  fallback/emission, no-color, and terminal-state cleanup.
- Audit focus, keyboard-only routes, non-color meaning, readable labels, and
  accessible text descriptions; defer broad screen-reader claims until audited.
- Add bounded real-workload profiling scenarios for transcript, tool output,
  resize, and active streaming.
- Revisit a screen-model investment only if the evidence strategy is shown
  inadequate or flaky.

**Acceptance criteria:**

- Opt-in terminal smokes leave terminal modes and control sequences clean.
- Capability fallbacks have readable textual behavior.
- Profile scenarios have stated input sizes, budgets, and repeatable collection
  instructions; no performance claim rests only on an anecdote.
- Any fixed 9x18 image-cell limitation remains documented until replaced.

### F7 — optional polish decisions

**Purpose:** make explicit product choices before widening AVA's surface.

**Scope:**

- Evaluate notifications, attention treatment, terminal title, standalone diff
  viewer, Which-Key/help overlay, and broader theme selection.
- Decide whether an item belongs in AVA at all, whether it requires a backend
  contract, and whether an existing compact alternative is sufficient.
- Keep all selected work behind explicit approval and a bounded implementation
  proposal.

**Acceptance criteria:**

- Each selected item has a decision record, owner, semantic contract, and test
  plan before implementation.
- Rejected items are recorded as excluded rather than slowly reintroduced.
- No optional surface expands the footer or changes default Up/Down behavior.

### F8 — release closure and documentation

**Purpose:** close approved frontend work with evidence and durable product
documentation.

**Scope:**

- Reconcile product, usage, terminal setup, keybinding, testing, and roadmap
  docs with actual shipped behavior.
- Review the baseline matrix, capture artifacts, known limitations, backend
  dependencies, and deferred decisions.
- Run the relevant deterministic, PTY, and full-suite validation for the
  shipped scope.

**Acceptance criteria:**

- Docs distinguish present behavior, approved deferral, and excluded scope.
- Every accepted frontend change maps to focused automated evidence or a
  stated manual terminal check.
- Diff whitespace and terminal cleanup checks pass.
- No deferred OpenCode surface is represented as AVA parity without approval.

## 9. First implementation slice: baseline-first visual/layout thread

The first implementation slice is intentionally visual/layout-only. It is a
plan, not shipped work. It must not alter backend contracts, composer input
semantics, session ownership, default arrow bindings, or footer content.

This is a narrow vertical thread through the roadmap, not permission to bypass
milestone order. F0 and its evidence baseline must be accepted first. The
remaining work then proceeds in F1-to-F5 order: responsive shell policy,
F2's generic transcript/card shell, composer preservation and command-overlay
constraints, general overlay geometry, and F5's detailed tool/permission work.
Each step inherits the acceptance criteria and dependency gates of its owning
milestone, does not claim that milestone complete, and stops if its prerequisite
evidence is not accepted.

### Deliverables

1. **Character-cell baseline matrix.** Use the completed F0 matrix at exact
   wide 160x48, ordinary 120x36, narrow 80x24, and short 100x12 dimensions as
   the before-redesign record. It reports the current implementation rather
   than an unreviewed compatibility promise.
2. **Named evidence.** Extend the completed F0 evidence catalog with stable
   names such as
   `frontend-wide-idle-shell`, `frontend-ordinary-active-run`,
   `frontend-narrow-command-palette`, `frontend-narrow-permission`,
   `frontend-permission-required-tool`, `frontend-short-selector`,
   `frontend-tool-result-compact`,
   `frontend-tool-result-expanded`, `frontend-diff-entry`,
   `frontend-session-navigation`, `frontend-plain-no-color`,
   `frontend-resize-redraw`, and `frontend-terminal-cleanup`.
3. **Responsive policy.** Define width/height breakpoints and sidebar policy:
   when it is visible, compacted, hidden, or invoked; how its state is
   disclosed; how modal focus returns; and which information cannot disappear.
4. **F2 generic transcript/card shell.** Establish only card placement,
   grouping, spacing, one primary summary line, generic collapsed/expanded
   placement, and no adjacent duplicate summary.
5. **F5 detailed tool/permission work.** Define status/target/outcome/duration
   anatomy; queued/pending, running, success, failed, canceled, denied,
   `permission required / awaiting decision`, truncated/spill, changed-path,
   and diff states; progressive inputs/output/IDs, specialized previews, copy,
   paths/diffs, permission linkage, and detailed evidence. The compact
   permission-required state visibly states the requirement and risk and points
   to an available decision action; its prompt/expanded detail retains reason,
   request identity, allow/reject choices, and follow-up.
6. **Overlay normalization.** Give palette, select-list, permission, and
   question overlays a shared geometry and narrow-width policy. Make permission
   choices concise without losing allow/deny/remembered-choice meaning.
7. **Preservation checks.** Prove the minimal footer and existing input/draft
   behavior remain intact, including plain Up/Down transcript-only scrolling.

### Slice exit evidence

- Renderer fixtures cover each named state that is deterministic in-process.
- Isolated tmux captures cover shell, narrow modal, selector, compact and
  expanded tool cards at 160x48, 120x36, 80x24, and short height, resize, and
  cleanup states that need a PTY. Deterministic narrow/plain fixtures and a
  Python tmux capture specifically cover the permission-required/awaiting-
  decision prompt/tool state, not merely allowed/denied outcomes.
- Capture review compares before/after hierarchy without claiming pixel-perfect
  terminal rendering.
- A control-sequence scan verifies saved text captures do not retain unexpected
  escape bytes, and PTY teardown restores terminal state.
- Any unavailable semantic field appears in the backend dependency ledger,
  rather than becoming a frontend inference.

## 10. Verification and evidence policy

Use repository wrappers, never raw `cmake` or `ctest`, so configured-tree
locking, parallelism, and project test conventions are preserved. Do not run
build and test wrappers concurrently in the same build tree.

For every interactive frontend change, the implementing coder/agent must add
or update deterministic C++ coverage first. When focus, cursor, resize, key
decoding, modal visibility, mouse behavior, or real screen composition is
affected, they must then add or update the closest isolated Python tmux
scenario. `tests/tui_tmux_smoke.py` together with
`tests/tui_smoke_helpers.py` is the required Python real-TTY harness for
interaction and layout behavior that unit rendering cannot prove. No handoff
may say only that it “looks correct from code.”

Focused deterministic work should start with the closest selected suite, for
example:

```sh
scripts/run-tests.sh --jobs 8 -R '^ava_tests\.tui_composer$'
```

For the ordinary-Space regression, the scenario calls the existing
evidence-saving helper after its cursor/no-palette assertions and persists
`composer-ordinary-space-no-completion.txt`. Run its exact isolated scenario,
then inspect and report that artifact together with the cursor
assertion/result:

```sh
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --jobs 1 -R '^ava_tui\.tmux_smoke_main_slash_completions$'
```

Run the thirteen isolated tmux scenarios only when the change touches their
behavior or required visual evidence:

```sh
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --jobs 13 -R '^ava_tui\.tmux_smoke_'
```

Run protocol-specific opt-ins when the implementation affects them:

```sh
AVA_TUI_KITTY_IMAGE_SMOKE=1 scripts/run-tests.sh --jobs 1 -R '^ava_tui\.kitty_image_smoke$'
AVA_TUI_OSC8_SMOKE=1 scripts/run-tests.sh --jobs 1 -R '^ava_tui\.osc8_smoke$'
```

When scope warrants it, run the full default suite through the wrapper:

```sh
scripts/run-tests.sh
```

Every documentation or implementation closure also runs:

```sh
git --no-pager diff --check
```

Run the targeted Python scenario and inspect its pane capture/evidence for
visible text, cursor position, dimensions, modal/menu absence or visibility as
appropriate, no leaked controls, and terminal cleanup; report what was tested
for real. Review visual captures at the baseline cell sizes rather than relying
only on text assertions. For PTY work, inspect terminal-state cleanup, expected
control-sequence emission, and absence of leaked control bytes in persisted
text evidence. Provider calls are unnecessary: use the fake provider or an
offline fixture for frontend visual validation.

## 11. Non-goals and deferred scope

The following are not implied by this roadmap:

| Item | Disposition |
| --- | --- |
| Pixel-perfect OpenCode clone | Excluded; AVA keeps a native terminal visual language |
| OpenTUI/Solid/reference architecture | Excluded; behavior reference only |
| Web, desktop, or SaaS surfaces | Excluded from this terminal roadmap |
| Thirty-three-theme count parity | Deferred; improve coherence before breadth |
| Audio or notifications by default | Deferred/opt-in only after F7 approval |
| Workspace or working-copy systems | Excluded from this frontend scope |
| Marketplace or plugin UI | Deferred behind stable plugin UI contracts and product approval |
| Share, update, telemetry, or upsell flows | Excluded |
| Automatic Up/Down draft/history behavior | Excluded; settled AVA control differs |
| Composer-footer metadata expansion | Excluded; settled minimal footer differs |
| Broad reusable component rewrite | Deferred unless separately approved with evidence |
| Heavy terminal dependency | Deferred unless separately approved with a clear payoff |
| Full diff viewer | Deferred behind F7 approval and stable semantic contracts |
| Broad screen-reader certification | Deferred pending a dedicated accessibility audit |

## 12. Decision ledger

| Decision | State | Rule for future work |
| --- | --- | --- |
| OpenCode is a behavior/quality reference, not source or architecture | Preserved | Compare outcomes only; do not port implementation patterns wholesale |
| Native C++23/ncursesw and narrow semantic seams | Preserved | Keep rendering out of backend contracts |
| Plain Up/Down scroll transcript only | Preserved | Do not move or replace drafts; vertical/history actions stay configurable and unbound by default |
| Minimal composer footer | Preserved | Show active model, known `ctx N`, and active spinner only |
| Ordinary Space completion policy | Preserved | Ordinary Space never opens file/reference completion; explicit Tab may force empty-token path suggestions, while real `@` and path-like prefixes remain valid triggers |
| Renderer tests plus PTY smokes as current evidence strategy | Preserved | Add a screen model only after demonstrated evidence failure or approval |
| Responsive sidebar/shell policy | Proposed | Define breakpoints and disclosure before implementation |
| Compact tool/result hierarchy | Proposed | Remove duplication while retaining inspectable detail |
| Shared overlay geometry and narrow permission choices | Proposed | Preserve keyboard, mouse, plain-mode, and safety meaning |
| Notifications, title, Which-Key, theme breadth, standalone diff viewer | Deferred | Require F7 decision, owner, and evidence plan |
| Session revert/redo, background-job controls, plugin UI | Deferred | Require stable backend semantic contracts first |
| Pixel-perfect clone, OpenTUI/Solid, web/SaaS, marketplace, upsell flows | Excluded | Do not add through frontend polish work |

## 13. Approval cut line

F0 recorded the evidence inventory and the narrow test-first ordinary-Space
regression closure described above. Its completion does not authorize F1 or
later visual redesign milestones, a broad frontend rewrite, a new terminal
dependency, an optional OpenCode-like surface, or a backend contract expansion.
Each later milestone proceeds only after its acceptance evidence and dependency
gates are reviewed.
