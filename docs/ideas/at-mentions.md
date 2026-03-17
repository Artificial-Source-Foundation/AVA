# @-Mention Syntax Parser

> Status: Idea (not implemented)
> Source: Multiple competitors (Cursor, Windsurf)
> Effort: Low

## Summary
Parses `@mentions` from user input to explicitly reference files (`@src/main.rs`), tools (`@read`, `@bash`), and URLs (`@https://example.com`). Returns cleaned input with mentions removed and a list of parsed references. Handles code block exclusion and email address filtering.

## Key Design Points
- Three mention types: `Mention::File`, `Mention::Tool`, `Mention::Url`
- 19 known tool names for tool mention matching
- Code fence and inline backtick exclusion (mentions inside code are ignored)
- Email exclusion: `user@domain` preceded by word character is not a mention
- `resolve_file_mention` checks if a mentioned file path actually exists on disk
- Mention text can contain `/`, `.`, `-`, `_`, `:`, `~`, `#`, `?`, `=`, `&`, `%`

## Integration Notes
- Would parse user input before sending to the agent, attaching file contents or tool hints
- Note: `ava-types` has its own `parse_mentions` that IS used; this was a more elaborate version in ava-agent
- Could auto-read mentioned files and prepend their contents to the user message
