# Contributing to AVA

## Branch Strategy

| Branch | Purpose | Protection |
|--------|---------|------------|
| `master` | Stable releases — tagged versions only | Protected: require PR + CI pass |
| `develop` | Active development — daily work lands here | Protected: require CI pass |
| `feature/*` | Feature branches — PRs into `develop` | None |
| `fix/*` | Bug fix branches — PRs into `develop` | None |
| `release/*` | Release prep branches — PRs into `master` | None |

### Workflow

1. Create a feature branch from `develop`: `git checkout -b feature/my-feature develop`
2. Make your changes, commit with [conventional commits](https://www.conventionalcommits.org/)
3. Push and create a PR targeting `develop`
4. CI must pass (rustfmt, clippy, tests, TypeScript check)
5. When `develop` is stable, create a `release/*` branch and PR into `master`
6. Tag the merge commit on `master` with `vX.Y.Z` to trigger the release pipeline

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add new feature
fix: fix a bug
chore: maintenance task
docs: documentation only
refactor: code restructuring
test: add or fix tests
perf: performance improvement
```

## Development

```bash
# Setup
git clone https://github.com/Artificial-Source-Foundation/AVA.git
cd AVA

# Rust (CLI + agent)
just check              # fmt + clippy + nextest
just test               # cargo nextest run --workspace
just run                # interactive TUI

# Desktop (SolidJS + Tauri)
pnpm install
pnpm run tauri dev

# Quick smoke test
cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openai --model gpt-5.4 --max-turns 3
```

## Release Checklist

1. Update `Cargo.toml` version
2. Update `docs/development/CHANGELOG.md`
3. PR from `release/vX.Y.Z` into `master`
4. After merge, tag: `git tag vX.Y.Z && git push --tags`
5. Release workflow builds CLI (5 platforms) + Desktop (4 platforms)
6. Edit the draft release on GitHub, publish
