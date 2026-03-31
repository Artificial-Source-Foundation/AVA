use ava_config::model_catalog::registry::{registry, RegisteredModel};
use ava_llm::ProviderKind;
use ava_types::Tool;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum PromptProfile {
    Standard,
    Lean,
}

fn prompt_profile(
    provider_kind: ProviderKind,
    model_name: &str,
    native_tools: bool,
) -> PromptProfile {
    if !native_tools {
        return PromptProfile::Standard;
    }

    let frontier_or_reasoning = registry_model_for_prompt(provider_kind, model_name)
        .map(|model| model.capabilities.reasoning)
        .unwrap_or(false);

    if frontier_or_reasoning {
        PromptProfile::Lean
    } else {
        PromptProfile::Standard
    }
}

fn model_supports_reasoning_for_prompt(provider_kind: ProviderKind, model_name: &str) -> bool {
    let model_lower = model_name.to_lowercase();
    registry_model_for_prompt(provider_kind, model_name)
        .map(|model| model.capabilities.reasoning)
        .unwrap_or_else(|| match provider_kind {
            ProviderKind::Anthropic | ProviderKind::Bedrock => {
                model_lower.contains("claude-opus-4.6")
                    || model_lower.contains("claude-sonnet-4.6")
                    || model_lower.contains("claude-opus-4-6")
                    || model_lower.contains("claude-sonnet-4-6")
                    || model_lower.contains("k2p5")
                    || model_lower.contains("kimi-k2.5")
            }
            ProviderKind::OpenAI | ProviderKind::AzureOpenAI | ProviderKind::Inception => {
                model_lower.starts_with("o3")
                    || model_lower.starts_with("o4")
                    || model_lower.contains("gpt-5")
                    || model_lower.contains("codex")
            }
            ProviderKind::Gemini => {
                model_lower.contains("gemini-2.5") || model_lower.contains("gemini-3")
            }
            ProviderKind::OpenRouter | ProviderKind::Copilot => {
                model_lower.contains("claude")
                    || model_lower.contains("gpt-5")
                    || model_lower.contains("codex")
                    || model_lower.starts_with("o3")
                    || model_lower.starts_with("o4")
                    || model_lower.contains("gemini-3")
            }
            ProviderKind::Ollama => false,
        })
}

fn provider_notes(title: &str, lines: &[&str]) -> String {
    let mut suffix = format!("## Provider notes ({title})\n");
    for line in lines {
        suffix.push_str("- ");
        suffix.push_str(line);
        suffix.push('\n');
    }
    suffix
}

/// Return provider-specific instructions to append to the base system prompt.
///
/// The core system prompt stays the same for all providers. This function returns
/// an additive suffix that optimizes instructions for each provider family's
/// tool-calling and reasoning conventions.
///
/// Returns `None` only when the base prompt needs no provider-specific tuning.
pub fn provider_prompt_suffix(provider_kind: ProviderKind, model_name: &str) -> Option<String> {
    let reasoning = model_supports_reasoning_for_prompt(provider_kind, model_name);
    match provider_kind {
        ProviderKind::Anthropic | ProviderKind::Bedrock => {
            let mut lines = vec![
                "Follow structured instructions closely and keep pre-tool prose minimal.",
                "Prefer one decisive tool/action at a time unless safe parallel work is obvious.",
                "After a tool failure, briefly explain the new plan instead of retrying blindly.",
            ];
            if reasoning {
                lines.push(
                    "Use extended or adaptive thinking only for genuinely hard tasks; keep visible reasoning terse.",
                );
            }
            Some(provider_notes("Anthropic-style", &lines))
        }
        ProviderKind::OpenAI | ProviderKind::AzureOpenAI | ProviderKind::Inception => {
            let mut lines = vec![
                "Use function calling for all tool interactions.",
                "Make tool arguments explicit and schema-accurate; prefer one well-formed call over speculative retries.",
                "Keep visible updates brief and action-oriented.",
            ];
            if reasoning {
                lines.push(
                    "Reasoning models work best with concise instructions and short visible summaries.",
                );
            } else {
                lines.push("Think briefly, then act.");
            }
            Some(provider_notes("OpenAI-style", &lines))
        }
        ProviderKind::Copilot => Some(provider_notes(
            "GitHub Copilot",
            &[
                "Copilot may proxy different backend families, so stick to plain function-calling patterns.",
                "Keep tool arguments short, explicit, and schema-accurate.",
                "If a tool call fails due to formatting, retry once with a simpler argument shape.",
            ],
        )),
        ProviderKind::Gemini => {
            let mut lines = vec![
                "Be explicit with tool arguments and expected outcomes.",
                "Read tool errors carefully and adjust before retrying.",
                "Keep progress updates short and structured.",
            ];
            if reasoning {
                lines.push("Use thinking for planning-heavy work, not trivial edits.");
            }
            Some(provider_notes("Google Gemini", &lines))
        }
        ProviderKind::OpenRouter => Some(provider_notes(
            "OpenRouter",
            &[
                "Backend routing can vary, so rely only on the documented tool contract.",
                "Keep tool calls conservative, explicit, and schema-accurate.",
                "If a call fails due to formatting, retry once with a simpler argument shape.",
            ],
        )),
        ProviderKind::Ollama => Some(provider_notes(
            "Ollama / local models",
            &[
                "Use short, concrete instructions and avoid unnecessary prose.",
                "Prefer one tool call at a time when the next step is uncertain.",
                "Re-check tool output before continuing; do not assume hidden state.",
            ],
        )),
    }
}

fn registry_model_for_prompt(
    provider_kind: ProviderKind,
    model_name: &str,
) -> Option<&'static RegisteredModel> {
    let registry = registry();

    let mut candidates = vec![model_name.to_string()];
    if let Some((_, tail)) = model_name.split_once('/') {
        candidates.push(tail.to_string());
    }
    if let Some(last) = model_name.rsplit('/').next() {
        candidates.push(last.to_string());
    }

    let provider_hint = match provider_kind {
        ProviderKind::Anthropic | ProviderKind::Bedrock => Some("anthropic"),
        ProviderKind::OpenAI | ProviderKind::AzureOpenAI | ProviderKind::Inception => {
            Some("openai")
        }
        ProviderKind::Copilot => None,
        ProviderKind::Gemini => Some("google"),
        _ => None,
    };

    for candidate in candidates {
        if let Some(provider) = provider_hint {
            if let Some(model) = registry.find_for_provider(provider, &candidate) {
                return Some(model);
            }
        }
        if let Some(model) = registry.find(&candidate) {
            return Some(model);
        }
        if let Some(normalized) = registry.normalize(&candidate) {
            if let Some(model) = registry.find(&normalized) {
                return Some(model);
            }
        }
    }

    None
}

/// Build a system prompt that tells the LLM it's an AI coding agent with tools.
///
/// For providers with native tool calling, this prompt is shorter (tool defs
/// are sent via the API). For text-only providers, tool schemas are embedded
/// in the prompt with the JSON envelope format.
pub fn build_system_prompt(
    tools: &[Tool],
    native_tools: bool,
    provider_kind: ProviderKind,
    model_name: &str,
    tool_visibility_profile: crate::routing::ToolVisibilityProfile,
) -> String {
    let mut prompt = String::with_capacity(2048);
    let profile = prompt_profile(provider_kind, model_name, native_tools);

    prompt.push_str(
        "You are AVA, an AI coding assistant. You help users with software engineering tasks \
         by reading files, writing code, running commands, and searching codebases.\n\n",
    );

    prompt.push_str("## Rules\n\n### Workflow\n");
    match tool_visibility_profile {
        crate::routing::ToolVisibilityProfile::AnswerOnly => {
            prompt.push_str("- Answer directly and concisely. Do not call tools unless they are explicitly provided and truly required.\n\n");
        }
        _ => {
            prompt.push_str(
                "- Read files before modifying them. Never guess at code you haven't seen.\n",
            );
            prompt.push_str(
                "- Do not re-read files you have already read or just edited in this conversation. The edit tool returns a diff confirming the change — trust it. Only re-read a file if another tool or process may have modified it since you last saw it.\n",
            );
            prompt.push_str(
                "- When editing multiple sections of the same file, batch them into as few edit calls as possible rather than making many small edits with re-reads between them.\n",
            );
            prompt.push_str(
                "- Follow instruction priority: system and tool rules first, then repo guidance, then the user's request.\n",
            );
            prompt.push_str("- Prefer native tools (read, edit, glob, grep) over bash equivalents — they are faster, sandboxed, and produce structured output.\n");
            prompt.push_str("- When calling multiple tools with no dependencies between them, make all independent calls in parallel. Combine turns whenever possible — use grep to find points of interest instead of reading many files individually.\n");
            prompt.push_str("- After a tool fails, adapt before retrying. Do not repeat the same call unchanged without new information.\n");
            if profile == PromptProfile::Standard {
                prompt.push_str("- Run tests after making changes when a test suite exists.\n");
                prompt.push_str("- For multi-step tasks, use `todo_write` to track progress. Mark items `in_progress` as you start them and `completed` when done.\n");
            } else {
                prompt.push_str("- Run tests when a test suite exists. Use `todo_write` only for genuinely multi-step work.\n");
            }
            prompt.push_str("- When your task is complete, call `attempt_completion` with a result describing what you did.\n\n");
        }
    }

    prompt.push_str("### Code discipline\n");
    prompt.push_str("- Do only what was asked. Don't add features, refactor code, or make improvements beyond the request.\n");
    prompt.push_str("- Never assume a library is available — check the manifest (package.json, Cargo.toml, etc.) first.\n");
    if profile == PromptProfile::Standard {
        prompt.push_str(
            "- Follow existing naming conventions, patterns, and formatting in the codebase.\n",
        );
        prompt.push_str(
            "- Prefer direct changes over speculative abstractions or extra comments.\n\n",
        );
    } else {
        prompt.push_str("- Match the existing codebase style and keep changes direct.\n\n");
    }

    prompt.push_str("### Executing with care\n");
    prompt.push_str("- Consider reversibility before destructive actions (force push, delete, rm -rf). Ask the user first for hard-to-reverse operations.\n");
    prompt.push_str("- When encountering obstacles, investigate — don't use destructive actions as shortcuts. Files you find may be in-progress work.\n");
    if profile == PromptProfile::Standard {
        prompt.push_str("- If your approach is blocked after a fair attempt, reconsider instead of brute-forcing.\n");
    } else {
        prompt.push_str("- Reconsider when blocked instead of brute-forcing.\n");
    }
    prompt.push_str("- Package installation commands (pip, npm, cargo add, etc.) run in a restricted sandbox. The .git and .ava directories are read-only. If pip fails with \"externally-managed-environment\", create a virtual environment first (`python -m venv .venv`). For npm, use local installs (not -g).\n\n");

    prompt.push_str("### Communication\n");
    prompt.push_str("- Minimize output tokens. Be concise while maintaining accuracy. Lead with the action or answer, not reasoning.\n");
    if profile == PromptProfile::Standard {
        prompt.push_str("- Aim for fewer than 4 lines of text output (excluding tool use) per response whenever practical.\n");
    }
    prompt.push_str("- After completing work, briefly confirm what you did. Do not explain your code or summarize your actions unless the user asks.\n");
    prompt.push_str("- When referencing code, use `file_path:line_number` format.\n");
    prompt.push_str("- Avoid filler, preamble (\"Here is...\", \"The answer is...\"), postamble, and unnecessary verbosity.\n");
    prompt.push_str("- Do the work without asking questions when the request is clear. Infer missing details from the codebase. Only ask when genuinely ambiguous.\n");
    prompt.push_str("- Distinguish directives (requests for action) from inquiries (requests for analysis). For inquiries, research and explain — do NOT modify files unless explicitly asked.\n");
    prompt.push_str("- Prioritize technical accuracy over validating beliefs. Disagree when the user is wrong. Objective guidance is more valuable than false agreement.\n\n");

    if tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
        && tools.iter().any(|tool| tool.name == "subagent")
    {
        prompt.push_str("### Delegation\n");
        prompt.push_str("- Keep small, single-file work in the main thread.\n");
        prompt.push_str("- Use `subagent` only for self-contained chunks whose result can be summarized back clearly.\n");
        prompt.push_str("- Prefer `scout` or `explore` for read-only reconnaissance, `plan` for design-only breakdowns, `review` for a final pass, and `worker` or `subagent` for isolated implementation.\n");
        prompt.push_str("- Use `background: true` when the sub-agent's work is independent and you can continue without its result. Use foreground (default) when you need the result before proceeding.\n");
        prompt.push_str("- Avoid chaining sub-agents for every step; delegate only when it saves context or speeds up exploration.\n");
        prompt.push_str("- After making significant multi-file edits or complex refactors, spawn a `review` subagent to catch bugs, security issues, and regressions. Skip review for trivial single-file fixes, config changes, or documentation edits.\n\n");
    }

    if native_tools && tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
    {
        prompt.push_str(
            "## Tool use\n\nUse the provided native tool/function calling interface for tool interactions.\n\n",
        );
    } else if !native_tools
        && tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
    {
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
    if tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
        && !tools.iter().any(|t| t.name == "attempt_completion")
    {
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
        let prompt = build_system_prompt(
            &tools,
            false,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );

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
        let prompt = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );

        assert!(prompt.contains("native tool/function calling interface"));
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
        let prompt = build_system_prompt(
            &tools,
            false,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );
        // Should only appear once (as part of the tool list, not the fallback section)
        let count = prompt.matches("### attempt_completion").count();
        assert_eq!(count, 1);
    }

    #[test]
    fn prompt_is_concise() {
        let tools = mock_tools();
        let prompt = build_system_prompt(
            &tools,
            false,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );
        // Should be well under 2000 tokens (~8000 chars) for 2 tools
        assert!(
            prompt.len() < 4500,
            "prompt too long: {} chars",
            prompt.len()
        );
    }

    #[test]
    fn lean_profile_is_shorter_for_frontier_native_models() {
        let tools = mock_tools();
        let standard = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );
        let lean = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-5.4",
            crate::routing::ToolVisibilityProfile::Full,
        );
        assert!(lean.len() < standard.len());
        assert!(lean.contains("Run tests when a test suite exists."));
    }

    #[test]
    fn lean_profile_uses_registry_for_copilot_model_aliases() {
        let tools = mock_tools();
        let lean = build_system_prompt(
            &tools,
            true,
            ProviderKind::Copilot,
            "claude-sonnet-4.6",
            crate::routing::ToolVisibilityProfile::Full,
        );
        assert!(lean.contains("Run tests when a test suite exists."));
    }

    #[test]
    fn answer_only_prompt_omits_tool_sections() {
        let tools = mock_tools();
        let prompt = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-5.4-mini",
            crate::routing::ToolVisibilityProfile::AnswerOnly,
        );

        assert!(!prompt.contains("## Tool use"));
        assert!(!prompt.contains("### attempt_completion"));
    }

    #[test]
    fn prompt_adds_delegation_guidance_when_subagent_tool_is_available() {
        let mut tools = mock_tools();
        tools.push(Tool {
            name: "subagent".to_string(),
            description: "Spawn a sub-agent".to_string(),
            parameters: json!({}),
        });
        let prompt = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-5.4",
            crate::routing::ToolVisibilityProfile::Full,
        );

        assert!(prompt.contains("### Delegation"));
        assert!(prompt.contains("Prefer `scout` or `explore`"));
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
        assert!(text.contains("Reasoning models") || text.contains("concise instructions"));
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
    fn ollama_suffix_mentions_local_model_guidance() {
        let suffix = provider_prompt_suffix(ProviderKind::Ollama, "llama3");
        let text = suffix.unwrap();
        assert!(text.contains("local") || text.contains("Ollama"));
    }

    #[test]
    fn azure_reuses_openai_style_suffix() {
        let suffix = provider_prompt_suffix(ProviderKind::AzureOpenAI, "gpt-5.4");
        let text = suffix.unwrap();
        assert!(text.contains("OpenAI-style"));
    }

    #[test]
    fn bedrock_reuses_anthropic_style_suffix() {
        let suffix = provider_prompt_suffix(ProviderKind::Bedrock, "claude-sonnet-4.6");
        let text = suffix.unwrap();
        assert!(text.contains("Anthropic-style"));
    }

    #[test]
    fn copilot_suffix_mentions_proxy_behavior() {
        let suffix = provider_prompt_suffix(ProviderKind::Copilot, "gpt-4o");
        let text = suffix.unwrap();
        assert!(text.contains("proxy") || text.contains("Copilot"));
    }
}
