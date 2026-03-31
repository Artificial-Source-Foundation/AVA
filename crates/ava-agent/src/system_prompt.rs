use ava_config::model_catalog::registry::{registry, RegisteredModel};
use ava_llm::ProviderKind;
use ava_types::Tool;

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

/// Return model-aware instructions to append to the base system prompt.
///
/// Tuning is based on the **model family** (Claude, GPT, Gemini, etc.) not the
/// provider — the same model behaves the same whether served through OpenAI,
/// Copilot, or OpenRouter. Provider-specific routing quirks (rate limits, proxy
/// behavior) are appended separately when relevant.
pub fn provider_prompt_suffix(provider_kind: ProviderKind, model_name: &str) -> Option<String> {
    let model_lower = model_name.to_lowercase();
    let reasoning = model_supports_reasoning_for_prompt(provider_kind, model_name);

    // ── Model-family tuning (primary) ────────────────────────────────
    let model_lines = model_family_notes(&model_lower, reasoning);

    // ── Provider-routing quirks (secondary, only when relevant) ──────
    let routing_lines = provider_routing_notes(provider_kind);

    if model_lines.is_empty() && routing_lines.is_empty() {
        return None;
    }

    let label = model_family_label(&model_lower, provider_kind);
    let mut all_lines = model_lines;
    all_lines.extend(routing_lines);
    Some(provider_notes(
        &label,
        &all_lines.iter().map(|s| s.as_str()).collect::<Vec<_>>(),
    ))
}

/// Model-family behavioral tuning — same model, same instructions regardless of provider.
fn model_family_notes(model_lower: &str, reasoning: bool) -> Vec<String> {
    let mut lines = Vec::new();

    if is_claude_model(model_lower) {
        // Claude Opus 4.6 / Sonnet 4.6 / Haiku 4.5 — up to 1M context
        lines
            .push("Follow structured instructions closely and keep pre-tool prose minimal.".into());
        lines.push(
            "Prefer one decisive tool/action at a time unless safe parallel work is obvious."
                .into(),
        );
        lines.push(
            "After a tool failure, briefly explain the new plan instead of retrying blindly."
                .into(),
        );
        lines.push("When given large contexts, prioritize the most recent instructions and tool results over older history.".into());
        if reasoning {
            if model_lower.contains("opus") {
                lines.push("Use adaptive thinking for genuinely hard tasks (architecture, complex refactors). For simple edits, keep thinking minimal.".into());
            } else {
                lines.push("Use extended thinking for hard tasks only. For simple edits, think briefly and act.".into());
            }
        }
    } else if is_codex_model(model_lower) {
        // GPT-5.3 Codex — 400K context, code-optimized, Responses API preferred
        lines.push(
            "Codex is optimized for code. Favor code output over prose. Minimize explanations."
                .into(),
        );
        lines.push("Maximize parallel tool calls — never read files one-by-one unless logically unavoidable.".into());
        lines.push("Keep preambles to 1 sentence + 1-2 sentence plan before tool calls. Update every 1-3 steps.".into());
        lines.push(
            "Keep visible output brief — 1-2 sentences at milestones, not play-by-play.".into(),
        );
        if reasoning {
            lines.push("Use medium reasoning for interactive coding, high/xhigh only for the hardest tasks.".into());
        }
    } else if is_gpt_model(model_lower) {
        // GPT-5.4 / o3 / o4 — up to 1M context, general purpose
        lines.push("Use function calling for all tool interactions. Make arguments explicit and schema-accurate.".into());
        lines.push("Prefer parallel function calls when operations are independent — this is a core strength.".into());
        lines.push("Keep visible text updates brief — function calls do the work, text is for status only.".into());
        if reasoning {
            lines.push(
                "Reasoning happens internally. Keep visible output to 1-2 sentence summaries."
                    .into(),
            );
        } else {
            lines.push("Think briefly, then act. Don't over-plan in visible output.".into());
        }
    } else if is_gemini_model(model_lower) {
        // Gemini 3.1 Pro / 3 Flash / 2.5 — up to 1M context
        lines.push(
            "Be explicit with tool argument types — Gemini is strict about schema compliance."
                .into(),
        );
        lines.push("Prefer grep/glob to discover files before reading individually. Extra turns are more expensive than larger reads.".into());
        lines.push(
            "Keep progress updates short and structured. Use numbered lists for multi-step plans."
                .into(),
        );
        if model_lower.contains("flash") {
            lines.push("Flash is optimized for speed. Keep tool chains short and focused.".into());
        }
        if reasoning {
            lines.push("Use thinking for planning-heavy work. For trivial edits, skip thinking and act directly.".into());
        }
    } else if is_deepseek_model(model_lower) {
        // DeepSeek V3.2 — 671B MoE, 128K context, open-weight
        lines.push("DeepSeek handles long code well. Read full files when needed rather than partial reads.".into());
        lines.push(
            "Keep tool arguments simple and explicit. Avoid deeply nested argument structures."
                .into(),
        );
        lines.push("Note: chat mode has an 8K output limit. Keep individual edits focused.".into());
        if reasoning {
            lines.push("DeepSeek reasoning is olympiad-level strong — use it for complex logic and math. Keep visible reasoning terse.".into());
        }
    } else if is_mercury_model(model_lower) {
        // Inception Mercury 2 — diffusion LLM, ~1000 tok/s, 128K context
        lines.push("Mercury is extremely fast (~1000 tok/s). Use this speed for iterative exploration — try, check, adjust quickly.".into());
        lines.push("Keep tool arguments simple. If a read returns empty, retry with explicit offset/limit rather than 0.".into());
        lines.push(
            "Favor many small focused tool calls over fewer large ones — latency is near-zero."
                .into(),
        );
    } else if is_grok_model(model_lower) {
        // xAI Grok 3/4 family — up to 2M context, strong tool calling
        lines.push(
            "Grok models are fast and direct. Match that tone — be concise, skip ceremony.".into(),
        );
        lines.push(
            "Prefer parallel tool calls when possible. Grok handles concurrent operations well."
                .into(),
        );
        if reasoning {
            lines.push("Grok reasoning is strong for code analysis and debugging. Keep visible output terse.".into());
        }
    } else if is_glm_model(model_lower) {
        // ZhipuAI GLM-4.7/5/5.1 family — 200K context, Ascend-trained
        lines.push("Keep tool arguments explicit and JSON-compliant. GLM models are strict about schema format.".into());
        lines.push(
            "GLM handles Chinese and English equally well. Match the user's language in responses."
                .into(),
        );
        lines.push("Prefer sequential tool calls over parallel when the task involves multiple dependent edits.".into());
        if reasoning {
            lines.push("GLM reasoning excels at system engineering and long-range agent tasks. Use for complex multi-file work.".into());
        }
    } else if is_kimi_model(model_lower) {
        // Kimi K2/K2.5 — 256K context, 1T MoE, agent swarm capable
        lines.push(
            "Kimi handles very long contexts (256K) well. Don't hesitate to read full files."
                .into(),
        );
        lines.push("Kimi excels at sustained multi-step tool use (100+ sequential calls). Plan ambitiously.".into());
        lines.push("Keep tool arguments simple and well-structured. Prefer explicit paths over globs when the target is known.".into());
        if reasoning {
            lines.push("Use Kimi's thinking mode for complex analysis. It supports 4 reasoning modes — thinking is the default for hard tasks.".into());
        }
    } else if is_minimax_model(model_lower) {
        // MiniMax M2/M2.5 — 205K context, Lightning Attention, SWE-bench leader
        lines.push("MiniMax models are strong at multi-file code editing. Batch related edits across files.".into());
        lines.push("Keep tool arguments explicit. MiniMax handles Rust, Java, Go, TypeScript, Python, C++ well.".into());
        if reasoning {
            lines.push("MiniMax reasoning is SWE-bench competitive. Use it for complex refactors and bug fixes.".into());
        }
    } else if is_qwen_model(model_lower) {
        // Alibaba Qwen3/Qwen3-Coder — up to 1M context, agentic coding
        lines.push("Qwen models support very long contexts (up to 1M). Use this for full-repo analysis when needed.".into());
        lines.push("Qwen-Coder excels at agentic tool calling and autonomous programming. Chain tool calls confidently.".into());
        if model_lower.contains("coder") {
            lines.push(
                "Qwen-Coder is optimized for code. Favor code output over explanations.".into(),
            );
        }
    } else if is_mistral_model(model_lower) {
        // Mistral Large 3 / Codestral 25.01 / Medium 3.1
        lines.push("Keep tool arguments concise. Mistral models work best with clear, direct instructions.".into());
        if model_lower.contains("codestral") {
            // Codestral: 256K context, 22B params, 80+ languages, FIM support
            lines.push("Codestral is optimized for code generation across 80+ languages. Favor code output over explanations.".into());
        }
        lines.push("Prefer one tool call at a time for complex chains. Parallel calls for independent reads.".into());
    } else if is_local_model(model_lower) {
        // Local / small models (Ollama, llama, etc.)
        lines.push("Use short, concrete instructions. Local models have smaller context windows — every token counts.".into());
        lines.push("Prefer one tool call at a time when the next step is uncertain.".into());
        lines.push("Re-check tool output before continuing — local models are more likely to hallucinate tool results.".into());
        lines.push("Keep code edits small and focused. Large multi-file refactors may exceed context limits.".into());
        lines.push(
            "If tool calling fails, describe what needs to change and let the user apply it."
                .into(),
        );
    }

    lines
}

/// Provider-level routing quirks — only for transport/proxy behavior, not model behavior.
fn provider_routing_notes(provider_kind: ProviderKind) -> Vec<String> {
    match provider_kind {
        ProviderKind::Copilot => vec![
            "Copilot has rate limits — minimize unnecessary tool calls. Batch reads when possible.".into(),
        ],
        ProviderKind::OpenRouter => vec![
            "OpenRouter routes to different backends. Keep tool calls schema-accurate — some backends are stricter.".into(),
        ],
        ProviderKind::Ollama => vec![
            "Running locally via Ollama. No network latency but limited by local hardware.".into(),
        ],
        _ => vec![],
    }
}

fn model_family_label(model_lower: &str, provider_kind: ProviderKind) -> String {
    if is_claude_model(model_lower) {
        "Claude".to_string()
    } else if is_codex_model(model_lower) {
        "Codex".to_string()
    } else if is_gpt_model(model_lower) {
        "GPT".to_string()
    } else if is_gemini_model(model_lower) {
        "Gemini".to_string()
    } else if is_deepseek_model(model_lower) {
        "DeepSeek".to_string()
    } else if is_mercury_model(model_lower) {
        "Mercury".to_string()
    } else if is_grok_model(model_lower) {
        "Grok".to_string()
    } else if is_glm_model(model_lower) {
        "GLM".to_string()
    } else if is_kimi_model(model_lower) {
        "Kimi".to_string()
    } else if is_minimax_model(model_lower) {
        "MiniMax".to_string()
    } else if is_qwen_model(model_lower) {
        "Qwen".to_string()
    } else if is_mistral_model(model_lower) {
        "Mistral".to_string()
    } else {
        format!("{provider_kind:?}")
    }
}

fn is_claude_model(m: &str) -> bool {
    m.contains("claude") || m.contains("haiku") || m.contains("sonnet") || m.contains("opus")
}
fn is_gpt_model(m: &str) -> bool {
    // GPT family but NOT Codex (Codex has its own tuning)
    (m.starts_with("gpt") || m.starts_with("o3") || m.starts_with("o4") || m.contains("gpt-"))
        && !m.contains("codex")
}
fn is_codex_model(m: &str) -> bool {
    m.contains("codex")
}
fn is_gemini_model(m: &str) -> bool {
    m.contains("gemini")
}
fn is_deepseek_model(m: &str) -> bool {
    m.contains("deepseek")
}
fn is_mercury_model(m: &str) -> bool {
    m.contains("mercury")
}
fn is_grok_model(m: &str) -> bool {
    m.contains("grok")
}
fn is_glm_model(m: &str) -> bool {
    m.contains("glm") || m.contains("codegeex")
}
fn is_kimi_model(m: &str) -> bool {
    m.contains("kimi") || m.contains("moonshot")
}
fn is_minimax_model(m: &str) -> bool {
    m.contains("minimax") || (m.starts_with("m2") && !m.contains("gemma"))
}
fn is_qwen_model(m: &str) -> bool {
    m.contains("qwen")
}
fn is_mistral_model(m: &str) -> bool {
    m.contains("mistral") || m.contains("mixtral") || m.contains("codestral")
}
fn is_local_model(m: &str) -> bool {
    m.contains("llama") || m.contains("phi-") || m.contains("starcoder") || m.contains("gemma")
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

// ── Prompt file templates (per model family) ────────────────────────────
const PROMPT_GPT: &str = include_str!("prompts/gpt.txt");
const PROMPT_CLAUDE: &str = include_str!("prompts/claude.txt");
const PROMPT_GEMINI: &str = include_str!("prompts/gemini.txt");
const PROMPT_DEFAULT: &str = include_str!("prompts/default.txt");

/// Select the base prompt template for the given model.
fn select_base_prompt(model_name: &str) -> &'static str {
    let m = model_name.to_lowercase();
    if is_claude_model(&m) {
        PROMPT_CLAUDE
    } else if is_codex_model(&m) || is_gpt_model(&m) || is_grok_model(&m) {
        PROMPT_GPT
    } else if is_gemini_model(&m) {
        PROMPT_GEMINI
    } else {
        PROMPT_DEFAULT
    }
}

/// Build a system prompt that tells the LLM it's an AI coding agent with tools.
///
/// Uses per-model-family prompt templates (loaded from `prompts/*.txt`) instead
/// of a generic one-size-fits-all prompt. Tool definitions are appended for
/// text-only providers; native tool callers get them via the API.
pub fn build_system_prompt(
    tools: &[Tool],
    native_tools: bool,
    _provider_kind: ProviderKind,
    model_name: &str,
    tool_visibility_profile: crate::routing::ToolVisibilityProfile,
) -> String {
    let mut prompt = String::with_capacity(4096);

    // Base prompt — model-family specific
    prompt.push_str(select_base_prompt(model_name));
    prompt.push('\n');

    // Sandbox note (all models)
    prompt.push_str("\n# Environment\n");
    prompt.push_str("Package installation commands (pip, npm, cargo add, etc.) run in a restricted sandbox. The .git and .ava directories are read-only. If pip fails with \"externally-managed-environment\", create a virtual environment first (`python -m venv .venv`). For npm, use local installs (not -g).\n");

    // Delegation (when subagent tool is available)
    if tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
        && tools.iter().any(|tool| tool.name == "subagent")
    {
        prompt.push_str("\n# Delegation\n");
        prompt.push_str("- Keep small, single-file work in the main thread.\n");
        prompt.push_str("- Use `subagent` only for self-contained chunks whose result can be summarized back clearly.\n");
        prompt.push_str("- Prefer `scout` or `explore` for read-only reconnaissance, `plan` for design-only breakdowns, `review` for a final pass, and `worker` or `subagent` for isolated implementation.\n");
        prompt.push_str("- Use `background: true` when the sub-agent's work is independent. Use foreground when you need the result before proceeding.\n");
        prompt.push_str("- After significant multi-file edits, spawn a `review` subagent. Skip review for trivial fixes.\n");
    }

    // Tool definitions (text-only providers embed schemas; native callers use API)
    if native_tools && tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
    {
        prompt.push_str(
            "\n# Tool use\nUse the provided native tool/function calling interface for tool interactions.\n",
        );
    } else if !native_tools
        && tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
    {
        prompt.push_str("\n# Tools\n\n");
        prompt.push_str(
            "To call tools, respond with ONLY a JSON object in this exact format:\n\
             ```json\n\
             {\"tool_calls\": [{\"name\": \"tool_name\", \"arguments\": {\"param\": \"value\"}}]}\n\
             ```\n\n\
             Do NOT mix tool calls with natural text.\n\n",
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

    // attempt_completion virtual tool
    if tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
        && !tools.iter().any(|t| t.name == "attempt_completion")
    {
        prompt.push_str("\n### attempt_completion\n");
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
        // Template-based prompts are larger but still reasonable for 2 tools
        assert!(
            prompt.len() < 6000,
            "prompt too long: {} chars",
            prompt.len()
        );
    }

    #[test]
    fn different_models_get_different_base_prompts() {
        let tools = mock_tools();
        let gpt = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-5.4",
            crate::routing::ToolVisibilityProfile::Full,
        );
        let claude = build_system_prompt(
            &tools,
            true,
            ProviderKind::Anthropic,
            "claude-sonnet-4.6",
            crate::routing::ToolVisibilityProfile::Full,
        );
        // GPT prompt has examples, Claude prompt has structured instructions
        assert!(gpt.contains("examples"));
        assert!(claude.contains("structured instructions"));
        // Both have core AVA identity
        assert!(gpt.contains("AVA"));
        assert!(claude.contains("AVA"));
    }

    #[test]
    fn copilot_claude_gets_claude_prompt() {
        let tools = mock_tools();
        let prompt = build_system_prompt(
            &tools,
            true,
            ProviderKind::Copilot,
            "claude-sonnet-4.6",
            crate::routing::ToolVisibilityProfile::Full,
        );
        // Should get Claude base prompt even through Copilot provider
        assert!(prompt.contains("structured instructions"));
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

        assert!(prompt.contains("# Delegation"));
        assert!(prompt.contains("scout") && prompt.contains("explore"));
    }

    // ── model-based prompt suffix tests ────────────────────────────────

    #[test]
    fn claude_model_gets_claude_tuning() {
        let suffix = provider_prompt_suffix(ProviderKind::Anthropic, "claude-sonnet-4");
        let text = suffix.unwrap();
        assert!(text.contains("Claude"));
        assert!(text.contains("structured instructions"));
    }

    #[test]
    fn claude_thinking_model_gets_thinking_note() {
        let suffix = provider_prompt_suffix(ProviderKind::Anthropic, "claude-sonnet-4.6");
        let text = suffix.unwrap();
        assert!(text.contains("thinking"));
    }

    #[test]
    fn claude_non_thinking_model_no_thinking_note() {
        let suffix = provider_prompt_suffix(ProviderKind::Anthropic, "claude-haiku-4");
        let text = suffix.unwrap();
        assert!(!text.contains("thinking"));
    }

    #[test]
    fn gpt_model_gets_function_calling() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenAI, "gpt-4.1");
        let text = suffix.unwrap();
        assert!(text.contains("function call"));
    }

    #[test]
    fn gpt_o_series_gets_reasoning_note() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenAI, "o3-mini");
        let text = suffix.unwrap();
        assert!(text.contains("Reasoning") || text.contains("internally"));
    }

    #[test]
    fn codex_model_gets_code_first_note() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenAI, "gpt-5.3-codex");
        let text = suffix.unwrap();
        assert!(text.contains("Codex") || text.contains("code"));
    }

    #[test]
    fn gemini_model_gets_schema_note() {
        let suffix = provider_prompt_suffix(ProviderKind::Gemini, "gemini-2.5-pro");
        let text = suffix.unwrap();
        assert!(text.contains("schema") || text.contains("explicit"));
    }

    #[test]
    fn deepseek_model_gets_tuning() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenAI, "deepseek-chat");
        let text = suffix.unwrap();
        assert!(text.contains("DeepSeek"));
    }

    #[test]
    fn mercury_model_gets_speed_note() {
        let suffix = provider_prompt_suffix(ProviderKind::Inception, "mercury-2");
        let text = suffix.unwrap();
        assert!(text.contains("Mercury") || text.contains("fast"));
    }

    #[test]
    fn ollama_local_model_gets_local_guidance() {
        let suffix = provider_prompt_suffix(ProviderKind::Ollama, "llama3");
        let text = suffix.unwrap();
        assert!(text.contains("Local") || text.contains("local"));
    }

    #[test]
    fn same_model_same_tuning_across_providers() {
        // GPT-5.4 should get GPT tuning whether from OpenAI, Copilot, or Azure
        let openai = provider_prompt_suffix(ProviderKind::OpenAI, "gpt-5.4").unwrap();
        let copilot = provider_prompt_suffix(ProviderKind::Copilot, "gpt-5.4").unwrap();
        let azure = provider_prompt_suffix(ProviderKind::AzureOpenAI, "gpt-5.4").unwrap();
        // All should contain GPT-specific tuning
        assert!(openai.contains("function call"));
        assert!(copilot.contains("function call"));
        assert!(azure.contains("function call"));
    }

    #[test]
    fn claude_on_copilot_gets_claude_tuning_plus_routing() {
        let suffix = provider_prompt_suffix(ProviderKind::Copilot, "claude-sonnet-4.6");
        let text = suffix.unwrap();
        // Should have Claude model tuning AND Copilot routing note
        assert!(text.contains("Claude"));
        assert!(text.contains("Copilot") || text.contains("rate limit"));
    }

    #[test]
    fn openrouter_adds_routing_note() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenRouter, "anthropic/claude-sonnet-4");
        let text = suffix.unwrap();
        assert!(text.contains("OpenRouter"));
    }
}
