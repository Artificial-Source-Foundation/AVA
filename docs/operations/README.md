# AVA Operations

These current operational guides cover source builds, validation, diagnosis, and release preparation. Runtime version `1.0.0` is not a published release. The release checklist defines the local artifact gate; it is not evidence of complete candidate qualification or publication.

- [Build configuration](build-configuration.md): CMake options, dependency modes, and build profiles.
- [Testing](testing.md): deterministic, optional, terminal, and live-provider test surfaces.
- [Docker build environment](docker/README.md) and its [build recipe](docker/Dockerfile): containerized Linux build workflow and limitations.
- [Terminal setup](terminal-setup.md): terminal capabilities, keyboard protocols, images, links, and clipboard helpers.
- [Troubleshooting](troubleshooting.md): symptom-first recovery guidance.
- [Diagnostics](diagnostics.md): passive doctor, private diagnostics, and sanitized support exports.
- [Release checklist](release-checklist.md): exact Linux host-artifact contract and local static package/provenance gates.
- [Official publication runbook](publication.md): required but not-yet-implemented draft/approval/publication, verification, rollback, and withdrawal design.
- [Artifact README source template](release-artifact-readme.md): source-only template installed as the future artifact documentation spine.
