# Prompt Injection Scanner

> Status: Idea (not implemented)
> Source: Original (safety research)
> Effort: Low

## Summary
Pattern-based scanner for detecting prompt injection attempts in tool outputs and user messages. Scores confidence based on the number and severity of matches, with recommendations ranging from Allow to Block. High-risk tool outputs (bash, read, web_fetch, grep) receive a 1.3x confidence multiplier.

## Key Design Points
- 15 regex patterns across three severity levels: High (0.45 weight), Medium (0.30), Low (0.15)
- High: "ignore previous instructions", "disregard your instructions", "new instructions:", "system prompt:", "forget everything", "override your"
- Medium: "you are now", "act as if", "pretend you are", "helpful assistant that", hidden HTML comments, markdown image injection
- Low: base64 blocks >100 chars, zero-width unicode characters
- Confidence clamped to [0.0, 1.0]; >0.7 = Block, >0.3 = Flag, >0 = Allow (low), 0 = Allow
- Case-insensitive matching via regex `(?i)` flag

## Integration Notes
- Would scan tool outputs before injecting them into the conversation
- Could be a middleware in the tool registry pipeline
- False positive rate needs tuning for real-world code content
