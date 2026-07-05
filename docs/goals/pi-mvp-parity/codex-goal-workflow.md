# Codex Goal Workflow For Pi Parity

## What Was Researched

OpenAI Codex goal documentation describes `/goal` as a persistent objective for work that should continue across turns until a defined outcome is true. The documented command surface is:

| Command | Use |
| --- | --- |
| `/goal <objective>` | Set a persistent goal. |
| `/goal` | Inspect the current goal. |
| `/goal pause` | Pause an active goal. |
| `/goal resume` | Resume a paused goal. |
| `/goal clear` | Clear the goal. |

The docs also state that goal mode may need to be enabled with:

```toml
[features]
goals = true
```

or with:

```sh
codex features enable goals
```

Local Codex skill guidance in `~/.codex/vendor_imports/skills/skills/.curated/define-goal/SKILL.md` adds the practical quality bar: goals must name the concrete outcome, verification evidence, scope, out-of-scope boundaries, and the stop condition for asking the user.

## Goal Shape For This Repo

Use this template when handing an area to Codex:

```text
/goal Bring AVA's <area> to documented Pi MVP parity by implementing or explicitly deferring every unchecked item in docs/goals/pi-mvp-parity/<area-file>.md. First read docs/product/mvp-baseline.md, docs/product/mvp-coverage-ledger.md, goals/ava-mvp-baseline-pi-tui/mvp-work-ledger.md, and the area file. Verify completion with targeted CTest/smoke commands plus git --no-pager diff --check, and update the product docs and area file with evidence or deferrals before marking the goal complete. Stop and ask if a Pi behavior requires a product decision, new remote-code trust policy, unsupported provider OAuth flow, or a destructive change outside the area scope.
```

Keep one area per goal. Do not create a goal for the entire `pi-mvp-parity` folder unless the user explicitly asks Codex to run a long multi-day product closure pass.

## Goal Quality Bar

Before using `/goal`, make sure the objective answers:

| Question | Required Answer |
| --- | --- |
| What concrete thing will be true? | One area reaches implemented, AVA-superior, deferred, or excluded status for every listed row. |
| What proves it? | Code/tests/docs/smoke evidence named in the area file. |
| What is in scope? | The files and subsystems listed in the area file. |
| What is out of scope? | Other area files unless required as a narrow dependency. |
| When should Codex stop and ask? | Product decision, trust/security policy expansion, unsupported provider auth, broad architecture rewrite, or repeated validation failure with no safe next step. |

## Checkpoints During A Goal

Codex should work in checkpoints, not one giant edit:

| Checkpoint | Required Output |
| --- | --- |
| 1. Baseline reconcile | Current AVA state, Pi reference paths inspected, rows selected for this batch. |
| 2. Implementation slice | One coherent PR-sized code/doc change with tests. |
| 3. Verification | Targeted commands run, failures fixed or blocker recorded. |
| 4. Docs sync | Product checklist, coverage ledger, and area file updated. |
| 5. Completion decision | Either area complete or next checkpoint clearly identified. |

If a goal lasts longer than one implementation slice, Codex should keep a concise progress note in the area file under `Progress Log` or in a companion file named `<area>.progress.md`.

## Completion Rule

Do not mark a goal complete just because some work landed. Mark it complete only when the area file's `100 Percent Criteria` section is satisfied or every remaining item has an explicit deferred/excluded decision with owner-level rationale.

## Recommended Goal Prompts

Use the suggested objective in each area file. Each objective is intentionally specific enough for Codex `/goal`, but short enough to fit the command line.

## External References

- OpenAI Codex goal use case: `https://developers.openai.com/codex/use-cases/follow-goals`
- OpenAI Codex goals cookbook: `https://developers.openai.com/cookbook/examples/codex/using_goals_in_codex`
- OpenAI Codex command reference: `https://developers.openai.com/codex/app/commands`
