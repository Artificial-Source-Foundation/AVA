---
report_status: complete
evidence_label: manual verified
client: Zed
client_version: "1.9.0"
client_commit: "ced90fc636c4ede05402befc38a63bae7fd741bd"
protocol: "ACP stable v1 schema-v1.19.0"
date_utc: "2026-07-14"
operator: "repository maintainer session"
---

# Zed 1.9.0 / AVA ACP evidence report

## Scope and label

- Requested label: `manual verified`
- Claimed scope: one credential-free Zed 1.9.0 lifecycle/tool/permission/client-filesystem/client-terminal flow and one in-flight cancellation flow against the exact AVA executable
- Explicit exclusions: other Zed versions, broad Zed compatibility, reconnect/resume behavior, unobserved features, real provider accounts, draft ACP v2, and other editors

## Version and commit

- Zed version: `1.9.0`
- Zed commit: `ced90fc636c4ede05402befc38a63bae7fd741bd`
- Zed CLI SHA-256: `0a28355104f7734a25c4dfd9e31412a8b8a964b39cd4bb6f0b4ce8ad0bb812c2`
- Zed editor SHA-256: `dd19659f10d90a868ba6f5782c65d3a652748669881791d82f9d13b3e24bd8c8`
- AVA version: `1.0.0`
- AVA binary SHA-256: `31969596f1236872aa8faad8ad6e4e4648dc1585a04055765c4ad3d835192283`
- Fake-provider version/source: AVA 1.0.0 test helper `tests/support/fake_provider_server.cpp`, built from source base commit `41bfdc9c65788c06b7a1345a07d129de1ee3024b` plus the reviewed uncommitted test-helper changes
- Fake-provider binary SHA-256: `61584947f1661a8b027c238c4c3283e1cddd8f252c19fdb754b98379af3ece76`
- ACP schema release: `schema-v1.19.0`
- ACP schema source commit: `a213df5240048f96d2b23f644984bb20c188a234`
- ACP schema fixture SHA-256: `92c1dfcda10dd47e99127500a3763da2b471f9ac61e12b9bf0430c32cf953796`
- Source base commit: `41bfdc9c65788c06b7a1345a07d129de1ee3024b`; the tested AVA and fake-provider binaries include reviewed uncommitted work, so their binary digests are the executable authority for this report

## Confinement record

- Mode: `sandbox`
- Zed/AVA container: network namespace `none`, read-only root, all capabilities dropped, no-new-privileges, bounded process/memory/CPU limits, bounded tmpfs mounts
- Graphical helper: separate network namespace `none`, read-only root, all capabilities dropped, no-new-privileges; its TLS VNC endpoint existed only on isolated container loopback and used test-only permit authentication for operator input transport
- Dedicated display: a private nested Weston Wayland compositor socket; Zed could not access the helper VNC endpoint or the host compositor
- Container image digest: `sha256:4fd1ef33035376defe35b3c83a357ce90086d15af47eabe20c766bc64d9b7977`
- Reviewed preflight SHA-256: `41638d49057b302f31c08aab4f23167e2189ad27cb8d56fecb2156f2427e05a5`
- Host-home denial: no host HOME or normal Zed profile was mounted
- External-network denial: both containers used network namespace `none`; AVA reached only the loopback fake provider in its own namespace
- D-Bus/keyring/SSH-agent/unrelated-process denial: absent from mounts and the allowlisted launcher environment; no Docker socket, host display socket, or host Wayland socket was available to Zed
- Temporary HOME/XDG clarification: state isolation only; it was not treated as the confinement boundary

## Commands

- Exact launcher shape: `env -i HOME=<isolated> PATH=/usr/bin:/bin TMPDIR=<isolated> <dogfood-script> zed run --ava <ava> --fake-provider <fake-provider> --zed <zed> --wayland-display <dedicated-socket> --acknowledge-dedicated-display --confinement sandbox --confinement-description <reviewed-description> --preflight-evidence <reviewed-file> --root-parent <private-root>`
- AVA agent command: `<AVA> --acp`
- Fake provider: loopback-only deterministic scenarios; exact private command arguments and raw request bodies were not copied

## Phase outcomes

### Lifecycle, tool, permission, client filesystem, and terminal

- [x] Initialization and new session observed
- [x] Final agent text and ordered tool cards observed
- [x] Tool-call lifecycle observed
- [x] Visible permission decisions observed and recorded
- [x] Client filesystem read and write operations observed
- [x] Client terminal operation observed
- [x] `end_turn`, thread archive, and clean client close observed
- Outcome: `pass`
- Observed facts: Zed listed the configured AVA external agent, launched the exact AVA subprocess, opened a new external-agent thread, and accepted the exact deterministic prompt. The UI showed `Read File`, `Grep`, `List Directory`, `Apply Patch`, and `Bash` cards. The operator selected `Allow once` for read access, patch read, patch write, and command execution. The client-owned target changed from `status: TODO` to `status: DONE`. Six deterministic provider exchanges completed, the terminal `cat` check succeeded, Zed displayed `E2E task complete: TODO fixed and verification command passed.`, and the running indicator ended normally. The completed thread was archived and Zed was quit; the AVA subprocess exited before phase cleanup.
- Inferred facts: the Bash card was served by negotiated ACP client-terminal support rather than AVA local-shell fallback, and exact-file bytes were served by negotiated Zed filesystem methods. This inference is bounded by AVA's tested ACP routing contract and the successful client-visible file/terminal effects.

### Cancellation

- [x] Delayed turn observed in flight
- [x] Cancellation initiated from Zed
- [x] Cancelled termination observed
- [x] Clean close observed
- Outcome: `pass`
- Observed facts: Zed launched a fresh AVA external-agent thread and sent the exact delayed-turn prompt. The provider captured exactly one in-flight request. While Zed showed the running indicator, the operator pressed Escape. The indicator disappeared without assistant final text. The persisted AVA session contained one `cancel` entry with reason `cancel_requested` and boundary `during_provider_request`, and contained no assistant message. The thread was archived and Zed was quit; Zed, AVA, and the provider were absent before final cleanup.
- Inferred facts: the disappearance of the running state was a client-originated ACP cancellation, not process teardown, because the cancel entry preceded thread archival/client exit and recorded the provider-request boundary.

## Cleanup

- Zed launcher cleanup: Zed was quit through its UI in both phases and was already absent when the script cleanup ran
- AVA descendant cleanup: the exact AVA subprocess exited after Zed closed; exact inherited phase-tag cleanup found no survivor
- Fake-provider cleanup: the lifecycle provider ended after the deterministic exchange; the cancellation provider ended after the cancelled connection; bounded cleanup found no survivor
- Residual-process check: both phase records report that the exact inherited phase-tag scan found no PID

## Evidence derivatives and redaction

- Raw logs/screenshots location: private confinement work root only; no raw artifact was copied into the repository
- Manually reviewed textual derivatives: exact version/commit and binary digests; operator outcome records; lifecycle request count and successful tool results; final client-owned target text; cancellation request count; the bounded cancel entry; cleanup residual results
- Screenshot derivatives: none committed; private compositor captures were reviewed only to direct the run and confirm visible states
- Temporary paths normalized: yes; private phase and workspace identifiers are omitted
- Credentials/fake keys/environment values: absent from this report
- Redaction reviewer: repository maintainer session plus the fail-closed `zed sanitize-copy` gate

## Observed versus inferred conclusion

- Observed: the exact Zed 1.9.0 build initialized and drove AVA over ACP, rendered tool and permission state, completed client-owned filesystem and terminal operations, displayed final text, sent in-flight cancellation, and closed without tagged process residue
- Inferred: successful behavior should extend only to matching Zed 1.9.0 configurations that negotiate the same ACP capabilities and preserve the same filesystem/terminal semantics
- Unsupported/unobserved: other Zed builds, other operating systems, reconnect/resume in Zed, session MCP configured by an untrusted client, image/audio prompts, and any feature not listed in the two observed phases
