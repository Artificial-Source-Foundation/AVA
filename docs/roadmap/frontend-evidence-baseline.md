# Historical pre-F1 baseline — F0 complete

**Status: Historical checkpoint complete — 2026-07-19**

This is a truthful historical record of AVA's frontend behavior at the F0
checkpoint before redesign. It is not the desired F1 look, a breakpoint
approval, or a compatibility promise. F1 and later work may change
presentation policy while preserving the semantic and interaction contracts
recorded here.

Current F1 runs generate the distinct `frontend-f1-*-idle-composer.txt`
artifacts documented in `docs/TESTING.md`. The F0 source checkpoint is the
reproduction authority for the old filenames and four-row observations below;
running the same scenario on current F1 source intentionally does not recreate
that historical presentation.

## Evidence layers

- **Deterministic C++ renderer/editor tests:** `ava_tests.tui_composer` covers
  composer/editor reducers, rows, dimensions, keyboard input, palettes,
  permission text, transcript and tool rendering. AVA intentionally has no
  deterministic terminal screen model.
- **Python/tmux real TTY:** the 13 isolated `ava_tui.tmux_smoke_*` scenarios
  exercise real screen composition, input, resize, private tmux cleanup, and
  sanitized pane captures under a fake provider or offline fixture.
- **Terminal protocol smokes:** opt-in Kitty image and OSC 8 tests cover their
  protocol emission/fallback boundaries; they are not substitutes for a visual
  review of every terminal.
- **No paid/live provider calls:** F0 uses isolated roots and fake/offline
  inputs only.

## Measured idle-shell policy

At the F0 checkpoint, the renderer showed a sidebar only when semantic sidebar
data was present and terminal width was at least **112** cells. Its maximum
width was **38** cells (the renderer used the smaller of 38 and one third of
terminal width). The matrix below records that F0 implementation, not approval
of a future F1 breakpoint or disclosure policy.

| Cells | F0 sidebar | Persisted evidence | Scenario | Asserted in the real TTY |
| --- | --- | --- | --- | --- |
| 160x48 | visible (`live session`/`Activity`) | `frontend-wide-idle-shell.txt` | `main_startup_trust_keybinds` | resize redraw; exact `160,48`; synchronized target composer/footer rows 45/46; no failure/C0 text; sidebar; plain capture |
| 120x36 | visible (`live session`/`Activity`) | `frontend-ordinary-idle-shell.txt` | `main_startup_trust_keybinds` | resize redraw; exact `120,36`; synchronized target composer/footer rows 33/34; no failure/C0 text; sidebar; plain capture |
| 80x24 | absent | `frontend-narrow-idle-shell.txt` | `main_startup_trust_keybinds` | resize redraw; exact `80,24`; synchronized target composer/footer rows 21/22; no failure/C0 text; sidebar absent; plain capture |
| 100x12 | absent | `frontend-short-idle-shell.txt` | `main_startup_trust_keybinds` | resize redraw; exact `100,12`; synchronized target composer/footer rows 9/10; no failure/C0 text; sidebar absent; plain capture |

Target rows are zero-based: after each resize, the scenario polls until
`Type a message...` is at `height - 3` and the one-line footer is immediately
below it, preventing tmux's physical resize from being accepted before AVA
reflows. It restores `120x32` and synchronizes/asserts rows 29/30 before
continuing its existing onboarding, trust, settings, and keybinding checks.

## Semantic-state inventory

“Authority/source seam” identifies existing semantic state, not a promise that
all states have a persisted screenshot. “Presenter seam” names the TUI boundary
at the checkpoint that consumed it. A future gap is named only where F0
real-TTY coverage was absent; it is a tracked target, not fabricated evidence.

| State | Semantic authority/source seam | Presenter seam | Deterministic evidence | Real-TTY/protocol evidence | F0 real-TTY gap / target |
| --- | --- | --- | --- | --- | --- |
| Idle/onboarding shell | runtime startup, onboarding, sidebar snapshot/model context | runtime/composer shell | `ava_tests.tui_composer` | four `frontend-*-idle-shell` captures | F1 visual hierarchy review |
| Editor, draft, completion | composer editor/input and completion state | composer input/palette | `ava_tests.tui_composer` | `main_editor_input`; `composer-ordinary-space-no-completion` | F3 narrow completion capture |
| Active run/streaming | runtime events, active-run queue, provider lifecycle | runtime transcript/composer | `ava_tests.tui_composer` | `active-run-follow-up-queued` | F2 streaming reading-order capture |
| Queued/restore | interactive run queue and restored draft state | runtime/composer | `ava_tests.tui_composer` | `active-run-follow-up-restored` | F3 queued-state visual treatment |
| Transcript, reasoning, error, compaction | transcript entries and runtime event state | transcript/text renderer | `ava_tests.tui_composer` | active-run flow exercises transcript; no dedicated persisted capture for these variants | F2 reasoning-expanded/streaming and retry/compaction transcript/status captures |
| Tool lifecycle and diff | tool lifecycle events, bounded tool output, changed-path/diff metadata | tool cards/diff presenter | `ava_tests.tui_composer` | `write-tool-card-success`, `visible-tool-details`, `visible-diff-card` | F5 failed/canceled/truncated/spill variants |
| Permission required, allow, deny | permission request/decision and tool lifecycle state | permission overlay/tool card | `ava_tests.tui_composer` | `permission-prompt-risk-request`, denied/success/audit captures | F5 permission-required compact/expanded matrix |
| Question prompt | question request/reply semantic event | question overlay | `ava_tests.tui_composer` | no persisted tmux capture | F5 question-prompt capture |
| Selectors, session tree, settings | command registry, model/session/settings state | palette/select-list/settings views | `ava_tests.tui_composer` | settings/model/provider/session captures | F4 short-modal focus capture |
| Attachments | composer attachment state and terminal capability fallback | composer attachment rows/image presenter | `ava_tests.tui_composer` | `slash-attach-palette`, `attachment-text-fallback` | F6 visual image-layout review |
| Themes and plain mode | display settings, environment, theme definitions | theme and renderer styling | `ava_tests.tui_composer` | `settings-plain-no-color`; theme scenarios | F6 theme visual (not plain-text) review |
| Terminal resize and cleanup | terminal events and guarded smoke lifecycle | terminal/runtime redraw | `ava_tests.tui_composer` | four shell resizes; `resize-redraw`; all scenarios clean private resources | F6 workload resize profiling |
| OSC 8 links | terminal link capability and emitted link spans | terminal/link presenter | deterministic terminal sanitization cases | `ava_tui.osc8_smoke` protocol-only | F6 visible link/fallback capture |
| Kitty/iTerm2 images | terminal image capability and image attachment state | terminal image presenter | deterministic fallback/image-row cases | `ava_tui.kitty_image_smoke` protocol-only for Kitty | F6 iTerm2 pixel verification |

## Persisted evidence catalog

Artifacts are created below `build/`; names here omit the `.txt` suffix where
the Python helper adds it. They are generated evidence, not committed fixtures.

| Scenario | Persisted names / evidence | Coverage classification |
| --- | --- | --- |
| `main_startup_trust_keybinds` | `startup-ready-composer`, `frontend-wide-idle-shell`, `frontend-ordinary-idle-shell`, `frontend-narrow-idle-shell`, `frontend-short-idle-shell`, `settings-plain-no-color`, `settings-model-selector`, `settings-scoped-model-selector`, `footer-context-count-refreshed` | idle/onboarding, exact resize matrix, plain settings/model context |
| `main_slash_completions` | `composer-ordinary-space-no-completion` | ordinary-Space regression: stable cursor/draft, no passive popup, forced Tab continuation |
| `active_run` | `active-run-follow-up-queued` | fake-provider active/queued run |
| `restore_followup` | `active-run-follow-up-restored` | restore follows queue ownership and keeps draft editable |
| `main_models_selectors` | `model-selector-arrow-scroll`, `provider-modal-arrow-navigation` | selector navigation and compact-height modal behavior |
| `main_permission_flow` | `permission-prompt-risk-request`, `permission-denied-tool-card`, `permission-denied-narrow-no-color`, `write-tool-card-success`, `visible-diff-card`, `visible-tool-details`, `permission-audit-summary` | permission prompt/deny/plain, successful tool, diff/detail, audit summary |
| `main_session_mgmt` | `session-selector` | session selector/tree navigation |
| `main_paste_scrollback_attach` | `large-paste-marker`, `resize-redraw`, `slash-attach-palette`, `attachment-text-fallback` | paste, scrollback, resize, attachment fallback |
| `suspend_resume`, `keybind_conflict`, `theme_env`, `theme_persisted`, `main_editor_input` | scenario assertions; no named saved text artifact | real-TTY-only behavioral coverage, no persisted capture catalog entry |
| `ava_tui.osc8_smoke` | protocol assertion output only | protocol-only; no saved pane text catalog artifact |
| `ava_tui.kitty_image_smoke` | protocol assertion output only | protocol-only Kitty emission/fallback; iTerm2 pixel behavior absent |
| `ava_tests.tui_composer` | test assertions, not pane captures | deterministic-only renderer/editor evidence |

## Known evidence gaps and named targets

F0 closes the inventory, matrix, catalog, capture policy, and historical shell
baseline. It does not claim the following visual work has shipped:

- **F2:** reasoning expanded/streaming and retry/compaction transcript/status
  captures.
- **F5:** question-prompt capture; detailed tool failure, canceled, truncated,
  and spill variants; permission-required compact/expanded matrix.
- **F4:** short-modal focus capture where existing selector navigation is not
  already a dedicated persisted frame.
- **F6:** terminal/theme/accessibility/protocol work: theme visual review
  rather than a plain-text assertion; iTerm2 pixel verification; screen-reader
  audit; workload resize/streaming profiling; OSC 8 visible fallback review.
- **F1–F3:** redesign review of shell hierarchy, responsive disclosure,
  transcript reading order, and completion geometry.

These are tracked targets with existing semantic sources where listed above;
they are not silent blockers to F0.

## Evidence artifact policy

- Generate artifacts only under the isolated scenario roots in `build/`, never
  commit them.
- Use fake providers or offline fixtures; F0 makes no paid or live-provider
  call.
- Capture exact cell dimensions and, when input semantics matter, the cursor
  position as well as visible screen text.
- Save only sanitized plain screen text. Inspect saved text for ESC and
  unexpected C0 control bytes (LF is allowed).
- Preserve private roots and verify tmux/fake-provider cleanup; report the
  real-TTY checks actually performed rather than inferring a visual result from
  code.

## Historical reproduction commands

At the F0 source checkpoint, run commands through the repository wrappers and
do not run build and test wrappers concurrently in one build tree. On current
F1 source, the targeted scenario uses the new F1 artifact names and two-row
composer geometry instead.

```sh
# Deterministic renderer/editor baseline
scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tests\.tui_composer$'

# Targeted F0 idle-shell baseline
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tui\.tmux_smoke_main_startup_trust_keybinds$'

# All 13 isolated tmux scenarios
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 13 -R '^ava_tui\.tmux_smoke_'

# Protocol opt-ins
AVA_TUI_KITTY_IMAGE_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tui\.kitty_image_smoke$'
AVA_TUI_OSC8_SMOKE=1 scripts/run-tests.sh --build-dir build --jobs 1 -R '^ava_tui\.osc8_smoke$'

# Documentation/patch hygiene
git --no-pager diff --check
```
