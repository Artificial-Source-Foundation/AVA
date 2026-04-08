use ava_config::model_catalog::registry::{registry, RegisteredModel};
use ava_llm::ProviderKind;
use ava_types::Tool;

/// Two-tier system prompt split into cacheable static prefix and dynamic suffix.
///
/// The static prefix contains identity, tool definitions, coding rules, and delegation
/// guidance — content that NEVER changes within a session. The dynamic suffix contains
/// provider-specific tuning, project instructions, plugin instructions, and dynamic rules.
///
/// `cache_boundary` is the byte offset where the static prefix ends and the dynamic
/// suffix begins. Providers that support prompt caching (e.g., Anthropic) should place
/// `cache_control` on the static prefix block only.
#[derive(Debug, Clone)]
pub struct SystemPromptParts {
    /// Identity, tool definitions, coding rules, delegation guidance — stable within a session.
    pub static_prefix: String,
    /// Provider tuning, project instructions, plugin injections — may change between turns.
    pub dynamic_suffix: String,
    /// Byte offset where static ends and dynamic begins (equals `static_prefix.len()`).
    pub cache_boundary: usize,
}

impl SystemPromptParts {
    /// Combine both parts into a single system prompt string.
    pub fn full_prompt(&self) -> String {
        if self.dynamic_suffix.is_empty() {
            self.static_prefix.clone()
        } else {
            format!("{}{}", self.static_prefix, self.dynamic_suffix)
        }
    }
}

fn model_supports_reasoning_for_prompt(provider_kind: ProviderKind, model_name: &str) -> bool {
    let model_lower = model_name.to_lowercase();
    registry_model_for_prompt(provider_kind, model_name)
        .map(|model| model.capabilities.reasoning)
        .unwrap_or_else(|| match provider_kind {
            ProviderKind::Anthropic => {
                model_lower.contains("claude-opus-4.6")
                    || model_lower.contains("claude-sonnet-4.6")
                    || model_lower.contains("claude-opus-4-6")
                    || model_lower.contains("claude-sonnet-4-6")
                    || model_lower.contains("k2p5")
                    || model_lower.contains("kimi-k2.5")
            }
            ProviderKind::OpenAI | ProviderKind::Inception => {
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

fn prompt_note_lines(contents: &str) -> Vec<String> {
    contents
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .filter_map(|line| line.strip_prefix('-').map(str::trim))
        .filter(|line| !line.is_empty())
        .map(ToString::to_string)
        .collect()
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
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CLAUDE));
        if reasoning {
            if model_lower.contains("opus") {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CLAUDE_OPUS_REASONING));
            } else {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CLAUDE_REASONING));
            }
        }
    } else if is_codex_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CODEX));
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CODEX_REASONING));
        }
    } else if is_gpt_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GPT));
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GPT_REASONING));
        } else {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GPT_FAST));
        }
    } else if is_gemini_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GEMINI));
        if model_lower.contains("flash") {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GEMINI_FLASH));
        }
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GEMINI_REASONING));
        }
    } else if is_deepseek_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_DEEPSEEK));
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_DEEPSEEK_REASONING));
        }
    } else if is_mercury_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_MERCURY));
    } else if is_grok_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GROK));
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GROK_REASONING));
        }
    } else if is_glm_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GLM));
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GLM_REASONING));
        }
    } else if is_kimi_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_KIMI));
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_KIMI_REASONING));
        }
    } else if is_minimax_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_MINIMAX));
        if reasoning {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_MINIMAX_REASONING));
        }
    } else if is_qwen_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_QWEN));
        if model_lower.contains("coder") {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_QWEN_CODER));
        }
    } else if is_mistral_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_MISTRAL));
        if model_lower.contains("codestral") {
            lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_MISTRAL_CODESTRAL));
        }
    } else if is_local_model(model_lower) {
        lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_LOCAL));
    }

    lines
}

/// Provider-level routing quirks — only for transport/proxy behavior, not model behavior.
fn provider_routing_notes(provider_kind: ProviderKind) -> Vec<String> {
    match provider_kind {
        ProviderKind::Copilot => prompt_note_lines(PROMPT_NOTES_PROVIDER_COPILOT),
        ProviderKind::OpenRouter => prompt_note_lines(PROMPT_NOTES_PROVIDER_OPENROUTER),
        ProviderKind::Ollama => prompt_note_lines(PROMPT_NOTES_PROVIDER_OLLAMA),
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
    m.contains("claude")
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
    m.contains("minimax")
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
        ProviderKind::Anthropic => Some("anthropic"),
        ProviderKind::OpenAI | ProviderKind::Inception => Some("openai"),
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
const PROMPT_NOTES_FAMILY_CLAUDE: &str = include_str!("prompts/families/claude.md");
const PROMPT_NOTES_FAMILY_CLAUDE_REASONING: &str =
    include_str!("prompts/families/claude-reasoning.md");
const PROMPT_NOTES_FAMILY_CLAUDE_OPUS_REASONING: &str =
    include_str!("prompts/families/claude-opus-reasoning.md");
const PROMPT_NOTES_FAMILY_CODEX: &str = include_str!("prompts/families/codex.md");
const PROMPT_NOTES_FAMILY_CODEX_REASONING: &str =
    include_str!("prompts/families/codex-reasoning.md");
const PROMPT_NOTES_FAMILY_GPT: &str = include_str!("prompts/families/gpt.md");
const PROMPT_NOTES_FAMILY_GPT_REASONING: &str = include_str!("prompts/families/gpt-reasoning.md");
const PROMPT_NOTES_FAMILY_GPT_FAST: &str = include_str!("prompts/families/gpt-fast.md");
const PROMPT_NOTES_FAMILY_GEMINI: &str = include_str!("prompts/families/gemini.md");
const PROMPT_NOTES_FAMILY_GEMINI_FLASH: &str = include_str!("prompts/families/gemini-flash.md");
const PROMPT_NOTES_FAMILY_GEMINI_REASONING: &str =
    include_str!("prompts/families/gemini-reasoning.md");
const PROMPT_NOTES_FAMILY_DEEPSEEK: &str = include_str!("prompts/families/deepseek.md");
const PROMPT_NOTES_FAMILY_DEEPSEEK_REASONING: &str =
    include_str!("prompts/families/deepseek-reasoning.md");
const PROMPT_NOTES_FAMILY_MERCURY: &str = include_str!("prompts/families/mercury.md");
const PROMPT_NOTES_FAMILY_GROK: &str = include_str!("prompts/families/grok.md");
const PROMPT_NOTES_FAMILY_GROK_REASONING: &str = include_str!("prompts/families/grok-reasoning.md");
const PROMPT_NOTES_FAMILY_GLM: &str = include_str!("prompts/families/glm.md");
const PROMPT_NOTES_FAMILY_GLM_REASONING: &str = include_str!("prompts/families/glm-reasoning.md");
const PROMPT_NOTES_FAMILY_KIMI: &str = include_str!("prompts/families/kimi.md");
const PROMPT_NOTES_FAMILY_KIMI_REASONING: &str = include_str!("prompts/families/kimi-reasoning.md");
const PROMPT_NOTES_FAMILY_MINIMAX: &str = include_str!("prompts/families/minimax.md");
const PROMPT_NOTES_FAMILY_MINIMAX_REASONING: &str =
    include_str!("prompts/families/minimax-reasoning.md");
const PROMPT_NOTES_FAMILY_QWEN: &str = include_str!("prompts/families/qwen.md");
const PROMPT_NOTES_FAMILY_QWEN_CODER: &str = include_str!("prompts/families/qwen-coder.md");
const PROMPT_NOTES_FAMILY_MISTRAL: &str = include_str!("prompts/families/mistral.md");
const PROMPT_NOTES_FAMILY_MISTRAL_CODESTRAL: &str =
    include_str!("prompts/families/mistral-codestral.md");
const PROMPT_NOTES_FAMILY_LOCAL: &str = include_str!("prompts/families/local.md");
const PROMPT_NOTES_PROVIDER_COPILOT: &str = include_str!("prompts/providers/copilot.md");
const PROMPT_NOTES_PROVIDER_OPENROUTER: &str = include_str!("prompts/providers/openrouter.md");
const PROMPT_NOTES_PROVIDER_OLLAMA: &str = include_str!("prompts/providers/ollama.md");

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
///
/// Returns [`SystemPromptParts`] with the prompt split into a cacheable static prefix
/// and a dynamic suffix. The static prefix contains identity, tool definitions, coding
/// rules, and delegation guidance. Providers that support caching should mark only
/// the static prefix with `cache_control`.
pub fn build_system_prompt(
    tools: &[Tool],
    native_tools: bool,
    _provider_kind: ProviderKind,
    model_name: &str,
    tool_visibility_profile: crate::routing::ToolVisibilityProfile,
) -> SystemPromptParts {
    let mut static_prefix = String::with_capacity(4096);

    // Base prompt — model-family specific
    static_prefix.push_str(select_base_prompt(model_name));
    static_prefix.push('\n');

    // Sandbox note (all models)
    static_prefix.push_str("\n# Environment\n");
    static_prefix.push_str("Package installation commands (pip, npm, cargo add, etc.) run in a restricted sandbox. The .git and .ava directories are read-only. If pip fails with \"externally-managed-environment\", create a virtual environment first (`python -m venv .venv`). For npm, use local installs (not -g).\n");

    // Delegation (when subagent tool is available)
    if tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
        && tools.iter().any(|tool| tool.name == "subagent")
    {
        static_prefix.push_str("\n# Delegation\n");
        static_prefix.push_str("- Keep small, single-file work in the main thread.\n");
        static_prefix.push_str("- Use `subagent` only for self-contained chunks whose result can be summarized back clearly.\n");
        static_prefix.push_str("- Prefer `scout` or `explore` for read-only reconnaissance, `plan` for design-only breakdowns, `review` for a final pass, and `worker` or `task` for isolated implementation.\n");
        static_prefix.push_str("- Use `background: true` when the sub-agent's work is independent. Use foreground when you need the result before proceeding.\n");
        static_prefix.push_str("- After significant multi-file edits, spawn a `review` subagent. Skip review for trivial fixes.\n");
    }

    // Tool definitions (text-only providers embed schemas; native callers use API)
    if native_tools && tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
    {
        static_prefix.push_str(
            "\n# Tool use\nUse the provided native tool/function calling interface for tool interactions.\n",
        );
    } else if !native_tools
        && tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
    {
        static_prefix.push_str("\n# Tools\n\n");
        static_prefix.push_str(
            "To call tools, respond with ONLY a JSON object in this exact format:\n\
             ```json\n\
             {\"tool_calls\": [{\"name\": \"tool_name\", \"arguments\": {\"param\": \"value\"}}]}\n\
             ```\n\n\
             Do NOT mix tool calls with natural text.\n\n",
        );

        for tool in tools {
            static_prefix.push_str(&format!("### {}\n", tool.name));
            static_prefix.push_str(&format!("{}\n", tool.description));
            static_prefix.push_str(&format!(
                "Parameters: {}\n\n",
                serde_json::to_string(&tool.parameters).unwrap_or_else(|_| "{}".to_string())
            ));
        }
    }

    // attempt_completion virtual tool
    if tool_visibility_profile != crate::routing::ToolVisibilityProfile::AnswerOnly
        && !tools.iter().any(|t| t.name == "attempt_completion")
    {
        static_prefix.push_str("\n### attempt_completion\n");
        static_prefix.push_str(
            "Call this when you have completed the task. \
             Parameters: {\"result\": \"description of what you did\"}\n",
        );
    }

    let cache_boundary = static_prefix.len();

    SystemPromptParts {
        static_prefix,
        dynamic_suffix: String::new(),
        cache_boundary,
    }
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
        let parts = build_system_prompt(
            &tools,
            false,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );
        let prompt = parts.full_prompt();

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
        let parts = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );
        let prompt = parts.full_prompt();

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
        let parts = build_system_prompt(
            &tools,
            false,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );
        let prompt = parts.full_prompt();
        // Should only appear once (as part of the tool list, not the fallback section)
        let count = prompt.matches("### attempt_completion").count();
        assert_eq!(count, 1);
    }

    #[test]
    fn prompt_is_concise() {
        let tools = mock_tools();
        let parts = build_system_prompt(
            &tools,
            false,
            ProviderKind::OpenAI,
            "gpt-4.1",
            crate::routing::ToolVisibilityProfile::Full,
        );
        let prompt = parts.full_prompt();
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
        )
        .full_prompt();
        let claude = build_system_prompt(
            &tools,
            true,
            ProviderKind::Anthropic,
            "claude-sonnet-4.6",
            crate::routing::ToolVisibilityProfile::Full,
        )
        .full_prompt();
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
        )
        .full_prompt();
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
        )
        .full_prompt();

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
        )
        .full_prompt();

        assert!(prompt.contains("# Delegation"));
        assert!(prompt.contains("scout") && prompt.contains("explore"));
    }

    #[test]
    fn system_prompt_parts_cache_boundary_is_correct() {
        let tools = mock_tools();
        let parts = build_system_prompt(
            &tools,
            true,
            ProviderKind::Anthropic,
            "claude-sonnet-4.6",
            crate::routing::ToolVisibilityProfile::Full,
        );
        // cache_boundary equals the static prefix length
        assert_eq!(parts.cache_boundary, parts.static_prefix.len());
        // static prefix is not empty
        assert!(!parts.static_prefix.is_empty());
        // dynamic suffix starts empty (provider suffix not yet appended)
        assert!(parts.dynamic_suffix.is_empty());
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
        // GPT-5.4 should get GPT tuning whether from OpenAI, Copilot, or Inception
        let openai = provider_prompt_suffix(ProviderKind::OpenAI, "gpt-5.4").unwrap();
        let copilot = provider_prompt_suffix(ProviderKind::Copilot, "gpt-5.4").unwrap();
        let inception = provider_prompt_suffix(ProviderKind::Inception, "gpt-5.4").unwrap();
        // All should contain GPT-specific tuning
        assert!(openai.contains("function call"));
        assert!(copilot.contains("function call"));
        assert!(inception.contains("function call"));
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
    fn bare_sonnet_name_does_not_get_claude_tuning() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenRouter, "sonnet-local-7b");
        let text = suffix.unwrap();
        assert!(!text.contains("Claude"));
        assert!(!text.contains("structured instructions"));
    }

    #[test]
    fn bare_m2_prefix_does_not_get_minimax_tuning() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenAI, "m2-ultra-custom");
        assert!(suffix.is_none());
    }

    #[test]
    fn openrouter_adds_routing_note() {
        let suffix = provider_prompt_suffix(ProviderKind::OpenRouter, "anthropic/claude-sonnet-4");
        let text = suffix.unwrap();
        assert!(text.contains("OpenRouter"));
    }
}
