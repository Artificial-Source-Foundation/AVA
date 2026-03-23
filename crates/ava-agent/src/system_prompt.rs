use ava_llm::ProviderKind;
use ava_types::Tool;

/// Return provider-specific instructions to append to the base system prompt.
///
/// The core system prompt stays the same for all providers. This function returns
/// an additive suffix that optimizes instructions for each provider family's
/// tool-calling and reasoning conventions.
///
/// Returns `None` for providers that work well with the default prompt.
pub fn provider_prompt_suffix(provider_kind: ProviderKind, model_name: &str) -> Option<String> {
    let model_lower = model_name.to_lowercase();
    match provider_kind {
        ProviderKind::Anthropic => {
            let mut suffix = String::from(
                "## Provider notes (Anthropic Claude)\n\
                 - Keep visible reasoning brief.\n\
                 - Keep pre-tool prose minimal unless it adds concrete value.\n",
            );
            let is_thinking_model = model_lower.contains("claude-opus-4.6")
                || model_lower.contains("claude-sonnet-4.6")
                || model_lower.contains("claude-opus-4-6")
                || model_lower.contains("claude-sonnet-4-6");
            if is_thinking_model {
                suffix.push_str(
                    "- Extended thinking is available; use it only for genuinely complex tasks.\n",
                );
            }
            Some(suffix)
        }
        ProviderKind::OpenAI => {
            let is_o_series = model_lower.starts_with("o3")
                || model_lower.starts_with("o4")
                || model_lower.contains("codex");
            let mut suffix = String::from(
                "## Provider notes (OpenAI)\n\
                 - Use function calling for all tool interactions.\n\
                 - Use parallel function calls when independent work can happen together.\n",
            );
            if is_o_series {
                suffix.push_str("- Keep visible reasoning focused on actions and results.\n");
            } else {
                suffix.push_str("- Think briefly, then act.\n");
            }
            Some(suffix)
        }
        ProviderKind::Gemini => Some(String::from(
            "## Provider notes (Google Gemini)\n\
                 - Be explicit with tool arguments.\n\
                 - Read tool errors carefully before retrying.\n",
        )),
        ProviderKind::OpenRouter => Some(String::from(
            "## Provider notes (OpenRouter)\n\
                 - Use function calling for all tool interactions.\n\
                 - Be explicit and concise in your tool arguments.\n",
        )),
        // Other providers use the base prompt without additions
        _ => None,
    }
}

/// Build a system prompt that tells the LLM it's an AI coding agent with tools.
///
/// For providers with native tool calling, this prompt is shorter (tool defs
/// are sent via the API). For text-only providers, tool schemas are embedded
/// in the prompt with the JSON envelope format.
pub fn build_system_prompt(tools: &[Tool], native_tools: bool) -> String {
    let mut prompt = String::with_capacity(2048);

    prompt.push_str(
        "You are AVA, an AI coding assistant. You help users with software engineering tasks \
         by reading files, writing code, running commands, and searching codebases.\n\n",
    );

    prompt.push_str("## Rules\n\n### Workflow\n");
    prompt.push_str("- Read files before modifying them. Never guess at code you haven't seen.\n");
    prompt.push_str("- Prefer native tools (read, edit, glob, grep) over bash equivalents — they are faster, sandboxed, and produce structured output.\n");
    prompt.push_str("- When calling multiple tools with no dependencies between them, make all independent calls in parallel.\n");
    prompt.push_str("- Run tests after making changes when a test suite exists.\n");
    prompt.push_str("- For multi-step tasks, use `todo_write` to track progress. Mark items `in_progress` as you start them and `completed` when done.\n");
    prompt.push_str("- When your task is complete, call `attempt_completion` with a result describing what you did.\n\n");

    prompt.push_str("### Code discipline\n");
    prompt.push_str("- Do only what was asked. Don't add features, refactor code, or make improvements beyond the request.\n");
    prompt.push_str("- Never assume a library is available — check the manifest (package.json, Cargo.toml, etc.) first.\n");
    prompt.push_str(
        "- Follow existing naming conventions, patterns, and formatting in the codebase.\n",
    );
    prompt.push_str("- Prefer direct changes over speculative abstractions or extra comments.\n\n");

    prompt.push_str("### Executing with care\n");
    prompt.push_str("- Consider reversibility before destructive actions (force push, delete, rm -rf). Ask the user first for hard-to-reverse operations.\n");
    prompt.push_str("- When encountering obstacles, investigate — don't use destructive actions as shortcuts. Files you find may be in-progress work.\n");
    prompt.push_str("- If your approach is blocked after a fair attempt, reconsider instead of brute-forcing.\n\n");

    prompt.push_str("### Communication\n");
    prompt
        .push_str("- Lead with the action or answer, not the reasoning. Be concise and direct.\n");
    prompt.push_str("- When referencing code, use `file_path:line_number` format.\n");
    prompt.push_str("- Avoid filler, time estimates, and unnecessary verbosity.\n\n");

    if native_tools {
        // Provider sends tool definitions via API — just list names for awareness.
        prompt.push_str("## Available Tools\n");
        for tool in tools {
            prompt.push_str(&format!("- **{}**: {}\n", tool.name, tool.description));
        }
        prompt.push('\n');
    } else {
        // Text-only provider — embed full schemas and specify the JSON envelope.
        prompt.push_str("## Tools\n\n");
        prompt.push_str(
            "To call tools, respond with ONLY a JSON object in this exact format:\n\
             ```json\n\
             {\"tool_calls\": [{\"name\": \"tool_name\", \"arguments\": {\"param\": \"value\"}}]}\n\
             ```\n\n\
             Do NOT mix tool calls with natural text. Either respond with a JSON tool call \
             or with natural text, never both.\n\n",
        );

        for tool in tools {
            prompt.push_str(&format!("### {}\n", tool.name));
            prompt.push_str(&format!("{}\n", tool.description));
            prompt.push_str(&format!(
                "Parameters: {}\n\n",
                serde_json::to_string(&tool.parameters).unwrap_or_else(|_| "{}".to_string())
            ));
        }
    }

    // Always include attempt_completion since it's a virtual tool.
    if !tools.iter().any(|t| t.name == "attempt_completion") {
        prompt.push_str("### attempt_completion\n");
        prompt.push_str(
            "Call this when you have completed the task. \
             Parameters: {\"result\": \"description of what you did\"}\n",
        );
    }

    prompt
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn mock_tools() -> Vec<Tool> {
        vec![
            Tool {
                name: "read".to_string(),
                description: "Read a file from disk".to_string(),
                parameters: json!({
                    "type": "object",
                    "properties": {
                        "path": {"type": "string", "description": "File path"}
                    },
                    "required": ["path"]
                }),
            },
            Tool {
                name: "bash".to_string(),
                description: "Execute a shell command".to_string(),
                parameters: json!({
                    "type": "object",
                    "properties": {
                        "command": {"type": "string", "description": "Command to run"}
                    },
                    "required": ["command"]
                }),
            },
        ]
    }

    #[test]
    fn text_prompt_contains_tool_names_and_schemas() {
        let tools = mock_tools();
        let prompt = build_system_prompt(&tools, false);

        assert!(prompt.contains("read"));
        assert!(prompt.contains("bash"));
        assert!(prompt.contains("Read a file from disk"));
        assert!(prompt.contains("Execute a shell command"));
        assert!(prompt.contains("tool_calls"));
        assert!(prompt.contains("attempt_completion"));
        assert!(prompt.contains("\"path\""));
    }

    #[test]
    fn native_prompt_lists_tools_without_json_envelope() {
        let tools = mock_tools();
        let prompt = build_system_prompt(&tools, true);

        assert!(prompt.contains("read"));
        assert!(prompt.contains("bash"));
        // Should NOT contain the JSON envelope instructions
        assert!(!prompt.contains("```json"));
        assert!(prompt.contains("attempt_completion"));
    }

    #[test]
    fn prompt_skips_attempt_completion_instructions_when_registered() {
        let tools = vec![Tool {
            name: "attempt_completion".to_string(),
            description: "Signal task completion".to_string(),
            parameters: json!({}),
        }];
        let prompt = build_system_prompt(&tools, false);
        // Should only appear once (as part of the tool list, not the fallback section)
        let count = prompt.matches("### attempt_completion").count();
        assert_eq!(count, 1);
    }

    #[test]
    fn prompt_is_concise() {
        let tools = mock_tools();
        let prompt = build_system_prompt(&tools, false);
        // Should be well under 2000 tokens (~8000 chars) for 2 tools
        assert!(
            prompt.len() < 4000,
            "prompt too long: {} chars",
            prompt.len()
        );
    }

    // ── provider_prompt_suffix tests ──────────────────────────────────

    #[test]
    fn anthropic_suffix_is_present() {
        let suffix = provider_prompt_suffix(ProviderKind::Anthropic, "claude-sonnet-4");
        assert!(suffix.is_some());
        let text = suffix.unwrap();
        assert!(text.contains("Anthropic"));
    }

    #[test]
    fn anthropic_thinking_model_gets_thinking_note() {
        let suffix = provider_prompt_suffix(ProviderKind::Anthropic, "claude-sonnet-4.6");
        let text = suffix.unwrap();
        assert!(
            text.contains("thinking"),
            "should mention thinking for 4.6 models"
        );
    }

    #[test]
    fn anthropic_non_thinking_model_no_thinking_note() {
        let suffix = provider_prompt_suffix(ProviderKind::Anthropic, "claude-haiku-4");
        let text = suffix.unwrap();
        // Haiku is not a thinking model, should not have thinking note
        assert!(!text.contains("thinking"));
    }

    #[test]
    fn openai_suffix_mentions_function_calling() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenAI, "gpt-4.1");
        let text = suffix.unwrap();
        assert!(text.contains("function calling") || text.contains("function call"));
    }

    #[test]
    fn openai_o_series_gets_reasoning_note() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenAI, "o3-mini");
        let text = suffix.unwrap();
        assert!(text.contains("actions and results"));
    }

    #[test]
    fn gemini_suffix_mentions_explicit_params() {
        let suffix = provider_prompt_suffix(ProviderKind::Gemini, "gemini-2.5-pro");
        let text = suffix.unwrap();
        assert!(text.contains("explicit") || text.contains("parameter"));
    }

    #[test]
    fn openrouter_suffix_present() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenRouter, "some/model");
        assert!(suffix.is_some());
    }

    #[test]
    fn ollama_no_suffix() {
        // Ollama uses generic prompts — no provider-specific suffix
        let suffix = provider_prompt_suffix(ProviderKind::Ollama, "llama3");
        assert!(suffix.is_none());
    }
}
