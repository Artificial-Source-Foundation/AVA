# Recipe System

> Status: Idea (not implemented)
> Source: Original
> Effort: Low

## Summary
Load reusable agent configurations from TOML files. Recipes define pre-packaged agent invocations with a prompt, optional model override, max turns, and tool selections. Recipes can be listed from a directory and sorted alphabetically.

## Key Design Points
- `Recipe` with name, description, prompt, optional model, optional max_turns, and tool list
- TOML format for human-readable configuration
- `load_recipe(path)` parses a single recipe file
- `list_recipes(dir)` scans a directory for `.toml` files, skips parse failures with warnings
- Empty tools list means "use defaults"
- Sorted alphabetically by name for consistent listing

## Integration Notes
- Recipes could live in `~/.ava/recipes/` and `.ava/recipes/`
- The existing workflow system (`--workflow plan-code-review`) covers similar use cases
- Could be exposed as a slash command: `/recipe code-review`
