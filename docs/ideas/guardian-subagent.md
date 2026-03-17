# Guardian Subagent

> Status: Idea (not implemented)
> Source: Original (safety research)
> Effort: High

## Summary
A dedicated review layer that assesses tool call risk via a numeric score (0-100) and auto-approves low-risk actions. Includes two implementations: `HeuristicGuardian` (fast, rule-based, no LLM needed) and `LlmGuardian` (uses a lightweight LLM call with heuristic fallback on timeout/parse failure).

## Key Design Points
- Three decisions: AutoApprove (<40), AskUser (40-79), Block (80+)
- `HeuristicGuardian` scores by tool name (read=10, bash=60), bash command patterns (ls=15, rm -rf /=95, sudo=90), and file path risk (/etc/=80, /tmp/=10, project=20)
- `LlmGuardian` sends a compact prompt asking for `SCORE|RATIONALE`, parses response, falls back to heuristic on timeout (5s), parse failure, or API error
- `cd dir && command` pattern handled by scoring the rest after `&&`
- Separate path risk analysis for system directories vs. project paths

## Integration Notes
- Would replace or augment the existing `DefaultInspector` in `ava-permissions`
- The existing `CommandClassifier` + `PermissionLevel` system covers much of this functionality
- LLM-based scoring adds latency; the heuristic alone may be sufficient
