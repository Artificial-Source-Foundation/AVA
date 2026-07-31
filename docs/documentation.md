# AVA Documentation Policy

This document defines AVA's documentation information architecture and
maintainer update rules. It is about where truth belongs, not a replacement for
any product or protocol contract.

## Information architecture

Use one primary home for each kind of information:

| Audience/purpose | Primary location | Content |
| --- | --- | --- |
| Repository entry | root [`README.md`](../README.md) | concise product summary, install/build start, links into current docs |
| Documentation navigation | [`docs/README.md`](README.md) | current documentation spine by audience |
| Users/operators | `docs/USAGE.md`, `docs/CONFIG.md`, topic guides | current commands, configuration, behavior, limits, troubleshooting |
| Client/extension authors | protocol and extension references in `docs/` | versioned normative wire/file/authoring contracts |
| Maintainers | `docs/architecture.md`, `docs/engineering/` | current architecture, source map, invariants, engineering policy |
| Security | `docs/security-sandboxing.md`, `docs/security/` | threat boundaries, verified guarantees, limitations, safe operation |
| Testing/release | `docs/TESTING.md`, `docs/release-checklist.md` | reproducible validation and release gates |
| Product state | `docs/product/` | current capability baselines and evidence mappings |
| Future work | `docs/roadmap/`, `docs/goals/`, named `*-plan.md` files | proposals, sequencing, acceptance criteria, unresolved work |
| Release history | `docs/versions/`, dated evidence/ledgers | what was claimed or observed at a particular point in time |
| Schemas/machine status | `docs/schema/`, JSON manifests | machine-readable contracts or status checked by tests |

Do not make a historical ledger, roadmap, or reference checkout the only place
that current user or maintainer behavior is explained. `docs/reference-code/`
(if present locally) is comparative material only: it is not AVA architecture,
implementation, or documentation authority and must not be included in broad
source-based claims.

## Document classes and precedence

Every document should make its class apparent from its title, opening, and
location.

### Normative contracts

A normative document tells an external consumer or maintainer what AVA must do.
It uses explicit language such as “must”, versioned fields, compatibility rules,
and validated limits. Current examples include:

- [`rpc-protocol.md`](rpc-protocol.md) for proprietary RPC v1;
- [`acp.md`](acp.md) and [`acp-support.json`](acp-support.json) for the supported
  ACP profile and evidence labels;
- [`headless-protocol.md`](headless-protocol.md) for shared headless behavior;
- [`session-format.md`](session-format.md) and
  [`engineering/session-versioning.md`](engineering/session-versioning.md) for
  persisted session compatibility;
- [`plugin-compatibility-policy.md`](plugin-compatibility-policy.md) for stable
  plugin/MCP compatibility rules;
- [`security/containment.md`](security/containment.md) for the verified command
  containment contract; and
- machine-readable schema/golden files where a test declares them normative.

A normative contract and its conformance tests must change together. If the
implementation and a normative document disagree, treat it as a defect: do not
silently “resolve” the discrepancy in a descriptive page. For current observed
behavior during debugging, production source plus passing focused tests is the
best evidence; for promised external compatibility, the versioned normative
contract defines the intended obligation.

### Descriptive current-state documents

A descriptive document explains the current implementation or use without
creating a new wire or compatibility promise. This includes
[`architecture.md`](architecture.md),
[`engineering/codebase-guide.md`](engineering/codebase-guide.md),
[`USAGE.md`](USAGE.md), [`CONFIG.md`](CONFIG.md), and topic guides. Descriptive
docs should link to normative details rather than restating large field tables,
config catalogs, or protocol examples.

Descriptive documents follow production source, CMake ownership, and tests. They
must say when a component is optional, experimental, platform-specific,
credential-gated, or not integrated.

### Planning documents

Roadmaps, goals, proposals, and `*-plan.md` documents describe desired work.
They are not evidence that a feature exists. Use future tense and explicit
statuses such as proposed, accepted, in progress, deferred, or complete. Once a
plan lands, update the current user/architecture/contract docs and either reduce
the plan to rationale or mark it historical; do not force readers to reconstruct
current behavior from completion checkboxes.

### History and evidence

Version files, dated interoperability reports, implementation ledgers, and
release journals preserve what was claimed, tested, or decided at a point in
time. They may remain intentionally stale. Date the evidence, identify the exact
version/commit or environment when relevant, and never cite it alone as proof of
current behavior. A “latest result” statement is evidence, not a timeless
contract.

## Source-of-truth matrix

| Question | Primary authority | Required corroboration/update |
| --- | --- | --- |
| What targets/files are built? | `CMakeLists.txt` files | architecture/codebase guide when module shape changes |
| Which module owns behavior? | production code under `src/ava/` and target/include dependency checks | [`architecture.md`](architecture.md), codebase guide |
| What does a CLI command or TUI control do? | implementation plus focused/whole-process tests | [`USAGE.md`](USAGE.md), topic guide, help text where applicable |
| Where/how is configuration loaded? | `src/ava/config/`, app loaders, tests | [`CONFIG.md`](CONFIG.md) and JSON schema if applicable |
| What bytes/fields may an RPC client rely on? | [`rpc-protocol.md`](rpc-protocol.md) | RPC serialization tests, contract manifest/goldens |
| What ACP behavior is supported? | [`acp.md`](acp.md), [`acp-support.json`](acp-support.json) | ACP tests and dated interoperability evidence |
| What is persisted in a session? | [`session-format.md`](session-format.md) and versioning policy | session serialization/validation/projection tests |
| What may a plugin/MCP author rely on? | plugin/MCP normative and compatibility docs | protocol/golden/fake-process tests |
| What is the security guarantee? | security docs scoped to a tested mechanism | production enforcement plus adversarial tests; never infer broader claims |
| Is a feature release-ready? | current product baseline and release checklist | default/focused test evidence; roadmap completion alone is insufficient |
| Why was a design chosen? | current architecture plus focused engineering rationale | history/ledger may provide dated context, not current authority |
| What should be built next? | roadmap/goal/plan document | status and acceptance criteria; no present-tense product claim |
| What dependency/license ships? | build pins, gitlinks, lockfiles, license/notices | provenance/package tests and `THIRD_PARTY_NOTICES.md` |

Source comments are authoritative for local invariants that cannot be separated
from an API or algorithm, but they are not a substitute for an external contract
or discoverable maintainer guide. Test names and fixtures prove covered behavior;
they should not be the sole user documentation.

## Public API boundary

AVA's supported public surfaces are the installed executable behavior and the
explicitly documented, versioned wire/file/extension contracts. **Headers under
`src/ava/` are internal C++ implementation details, not a stable public library
API.** CMake `PUBLIC` linkage and public header visibility express transitive
build requirements inside this repository; they do not promise source, binary,
or ABI compatibility to external C++ consumers. Do not describe an internal
class or header as supported integration API unless AVA deliberately introduces
and documents such a contract.

## Style rules

- Lead with purpose and audience. State whether the page is normative,
  descriptive, planning, historical, experimental, or evidence.
- Prefer repository-relative links and exact source/test paths in backticks.
- Use one canonical explanation and link to it. Do not copy protocol field
  catalogs, configuration lists, security guarantees, or long commands across
  several pages.
- Use present tense only for behavior implemented and covered now. Use future
  tense and a visible status for proposals.
- Qualify platform, build-option, credential, trust, and opt-in requirements.
  “Supported” must identify the supported surface and evidence.
- Use stable machine identifiers exactly; use prose labels for people. Do not
  branch a documented client on free-form diagnostic text.
- Keep examples credential-free and use placeholders such as
  `/absolute/path/to/ava`. Never include secrets, private provider payloads, or
  unsanitized support artifacts.
- Link source directories/files instead of pasting large implementation blocks.
  Small snippets are appropriate only when they define a user/client operation.
- Use GitHub-rendered Markdown. Prefer tables and fenced text diagrams. Mermaid
  is acceptable only when GitHub renders the used syntax reliably; include an
  equivalent textual explanation so the architecture remains usable in plain
  renderers.
- Use descriptive link text, unique headings, fenced language tags, and
  repository-standard lowercase `.md` names except established uppercase entry
  guides.
- Avoid line-number links to a moving branch. Exact commit permalinks are
  appropriate for external evidence; current in-repository docs should link to
  paths and symbols.

## Freshness rules

A current-state document has no implied grace period: update it in the same
change that makes its statement false. Review these triggers:

- An entry point, module responsibility, dependency direction, process, or
  authority boundary changes: update architecture and the codebase guide.
- User-visible behavior, defaults, commands, configuration paths, environment
  variables, limits, or troubleshooting changes: update the owning user/topic
  guide.
- A stable serialized shape or compatibility rule changes: update the normative
  contract, version policy as needed, tests, manifests/schemas, and goldens in
  the same patch.
- A security control or limitation changes: update the precise security page.
  Do not leave a broader stale claim elsewhere.
- A planned item becomes implemented/deferred/rejected: update its status and
  the current-state docs; capability/evidence ledgers must point to current
  tests.
- A release/evidence result is recorded: include date, exact scope, expected
  skips/gates, and whether live credentials/network were used. Never replace a
  reproducible command with only a result claim.
- A file is renamed or moved: update inbound links and navigation in the same
  patch.

For broad edits, search for the old term, path, option, field, version, and claim
across `README.md`, `docs/`, source help text, schemas, fixtures, and tests.
Exclude build trees, vendored/reference code, and historical ledgers when
looking for current architectural authority; update historical text only when it
contains a broken link or is explicitly labeled as current.

## Required documentation updates by change type

| Change | Minimum documentation review/update |
| --- | --- |
| New/changed CLI command, flag, alias, or exit behavior | root/help output as applicable, `USAGE.md`, `CONFIG.md` if configured, headless docs for automation behavior |
| TUI control, rendering, keybinding, theme, or terminal capability | `USAGE.md`, `themes-keybindings.md` and/or `terminal-setup.md`; testing evidence when a real terminal is required |
| Model/provider/auth support | `providers.md`, `CONFIG.md`, user setup; architecture only for a new seam |
| RPC event/request/response or framing | normative `rpc-protocol.md`, contract manifest/goldens, headless docs, version/compatibility statement |
| ACP method/capability/interoperability | `acp.md`, `acp-support.json`, tests and dated evidence policy/report when a client claim changes |
| Session envelope/payload/projection/import/export | `session-format.md`, session-versioning policy, protocol docs that expose it, migration/compatibility tests |
| Built-in tool or permission operation | `USAGE.md`, security guide when authority changes, side-effect checklist, protocol tool/event docs as applicable |
| Command policy/containment/process cleanup | security-sandboxing and containment contract; exact limitations and platform fallbacks |
| Plugin/MCP authoring or wire behavior | plugin/MCP docs and compatibility policy; golden/schema fixtures |
| Context/trust/resource discovery | `CONFIG.md`, `context-resources.md`, security trust section |
| Module/entry-point/dependency/ownership change | `architecture.md`, engineering codebase guide |
| Build dependency, option, prerequisite, or test runner | `CONTRIBUTING.md`, `TESTING.md`, release/provenance docs where applicable |
| Desktop prototype capability | `desktop-qml.md`, codebase guide; keep experimental/integration status explicit |
| Roadmap/capability status | owning plan/roadmap plus current product baseline/evidence ledger; user docs if shipped |
| Release packaging/install contract | release checklist, artifact README, testing commands, notices/provenance as applicable |

“No documentation impact” is acceptable for an internal refactor only after
checking that paths, ownership, diagrams, tests, and user-observable behavior
remain truthful.

## Link-validation workflow

The repository does not currently declare a single normative Markdown link
checker. Until one is selected, maintainers must perform these steps:

1. Review every changed relative link from the directory containing the source
   Markdown file; verify the destination exists with exact case.
2. For changed same-page or cross-page fragments, render with GitHub-compatible
   heading rules and click the fragment in a preview when possible.
3. Search renamed/deleted documentation paths for inbound references.
4. Review absolute external links manually when they are material to setup or a
   compatibility claim; record the review date only for evidence that needs it.
5. Run any documentation-link CTest present in the configured tree. If no such
   test is listed, record that automated link validation was unavailable rather
   than claiming it passed.
6. Always run `git --no-pager diff --check`.

Reserved workflow placeholders for future automation:

```sh
# TODO(docs-tooling): configure a repository-owned Markdown relative-link and
# fragment checker, then place its exact local command here.

# TODO(ci-docs): add the same checker as a required credential-free CI/CTest
# job and document its CTest name here.

# TODO(external-links): decide whether scheduled external-link checking is
# useful; keep it non-release-blocking unless ownership and retry policy exist.
```

A future checker must ignore build trees and local `docs/reference-code/`, avoid
network access for the required local gate, understand GitHub heading fragments,
and report source file/line plus destination. Tool adoption is incomplete until
the command, version/pin, exclusions, and CI behavior are documented here.
