# Reference Repositories

Shallow clones in `docs/reference-code/` for architecture comparison and feature gap analysis.

| Repo | URL | SHA | Last Pulled | Key Takeaways |
|------|-----|-----|-------------|---------------|
| aider | https://github.com/Aider-AI/aider.git | b23516061 | 2026-02-25 | Python agent, repo map with tree-sitter, edit formats |
| cline | https://github.com/cline/cline.git | f10b6f3 | 2026-02-25 | VSCode extension, streaming tool use, plan/act modes |
| gemini-cli | https://github.com/google-gemini/gemini-cli.git | 4a78a96 | 2026-02-25 | Ink TUI, sandboxed shell, built-in tools |
| goose | https://github.com/block/goose.git | fc292c7 | 2026-02-25 | Rust agent, extension system, MCP-first |
| opencode | https://github.com/sst/opencode.git | b8337cdd | 2026-02-25 | Go TUI, minimal core, LSP integration |
| openhands | https://github.com/All-Hands-AI/OpenHands.git | 7f3af37 | 2026-02-25 | Python, Docker sandbox, agent delegation |
| plandex | https://github.com/plandex-ai/plandex.git | e2d7720 | 2026-02-25 | Go, plan-based, diff accumulation |
| pi-mono | https://github.com/badlogic/pi-mono.git | 9a0a8d7 | 2026-02-25 | ~5-file agent core, everything via extensions, minimal design |

## Updating

```bash
cd docs/reference-code
for repo in aider cline gemini-cli goose opencode openhands plandex pi-mono; do
  echo "=== $repo ===" && cd "$repo" && git pull --ff-only && cd ..
done
```

**Note:** cline uses git-lfs — may need `GIT_LFS_SKIP_SMUDGE=1` or `git-lfs` installed.
