# Network Access Policy

> Status: Idea (not implemented)
> Source: Original (safety)
> Effort: Medium

## Summary
Domain-level allow/deny lists for controlling outbound network access from tool processes. Provides the policy layer with enforcement intended via proxy environment variables injected into subprocess environments.

## Key Design Points
- Three actions: Allow, Deny, Ask
- Default policy: Ask for unknown domains, always allow common dev infrastructure (github.com, npmjs.org, crates.io, pypi.org, localhost, etc.)
- Deny list takes priority over allow list
- Wildcard prefix matching: `*.example.com` matches `sub.example.com` but not `example.com`
- Session-approved domains persist for the session duration
- `to_env_vars(proxy_addr)` generates http_proxy/https_proxy/no_proxy environment variables
- Case-insensitive domain matching
- `permissive()` and `restrictive()` factory methods for preset policies

## Integration Notes
- Would integrate with the sandbox executor to inject proxy env vars into subprocesses
- Needs an actual HTTP proxy implementation for enforcement (not just policy)
- The existing sandbox system handles filesystem isolation; this would add network isolation
