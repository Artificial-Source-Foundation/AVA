# GitHub Issue Resolver

> Status: Idea (not implemented)
> Source: OpenHands
> Effort: Low

## Summary
Parses GitHub issue URLs, fetches issue details via the `gh` CLI, and generates a structured goal message for the agent. Supports full URLs (`https://github.com/owner/repo/issues/123`) and short format (`owner/repo#123`).

## Key Design Points
- `IssueRef` with owner, repo, and issue number
- `parse_issue_ref` handles full GitHub URLs (with query params) and `owner/repo#123` short format
- `fetch_issue` shells out to `gh issue view --json title,body,labels,state,comments`
- `issue_to_goal` generates a structured prompt including title, body (truncated at 3000 chars), labels, comment count
- Goal message ends with: "Please analyze the issue, find the relevant code, implement a fix, and verify it works."

## Integration Notes
- Could be a slash command (`/issue https://github.com/...`) or auto-detected from user input
- Requires `gh` CLI to be installed and authenticated
- The task tool could spawn a sub-agent with the generated goal message
