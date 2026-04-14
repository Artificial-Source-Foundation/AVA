use ava_config::model_catalog::registry::{registry, RegisteredModel};
use ava_llm::ProviderKind;
use ava_types::Tool;

#[derive(Debug, Clone, Default, serde::Serialize, serde::Deserialize)]
pub struct BenchmarkPromptOverride {
    #[serde(default)]
    pub family: Option<String>,
    #[serde(default)]
    pub prompt_file_contents: Option<String>,
}

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

fn normalize_prompt_family(family: &str) -> Option<&'static str> {
    match family.trim().to_lowercase().as_str() {
        "claude" | "anthropic" => Some("claude"),
        "codex" => Some("codex"),
        "gpt" | "openai" => Some("gpt"),
        "gemini" | "google" => Some("gemini"),
        "deepseek" => Some("deepseek"),
        "mercury" | "inception" => Some("mercury"),
        "grok" | "xai" => Some("grok"),
        "glm" | "codegeex" => Some("glm"),
        "kimi" | "moonshot" => Some("kimi"),
        "minimax" => Some("minimax"),
        "qwen" => Some("qwen"),
        "mistral" | "codestral" | "mixtral" => Some("mistral"),
        "local" | "llama" | "phi" | "gemma" => Some("local"),
        "generic" | "default" => Some("generic"),
        _ => None,
    }
}

fn prompt_family_display_label(family: &str) -> String {
    match family {
        "claude" => "Claude".to_string(),
        "codex" => "Codex".to_string(),
        "gpt" => "GPT".to_string(),
        "gemini" => "Gemini".to_string(),
        "deepseek" => "DeepSeek".to_string(),
        "mercury" => "Mercury".to_string(),
        "grok" => "Grok".to_string(),
        "glm" => "GLM".to_string(),
        "kimi" => "Kimi".to_string(),
        "minimax" => "MiniMax".to_string(),
        "qwen" => "Qwen".to_string(),
        "mistral" => "Mistral".to_string(),
        "local" => "Local".to_string(),
        "generic" => "Generic".to_string(),
        other => other.to_string(),
    }
}

fn prompt_lines_for_family(family: &str, model_lower: &str, reasoning: bool) -> Vec<String> {
    match family {
        "claude" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_CLAUDE);
            if reasoning {
                if model_lower.contains("opus") {
                    lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CLAUDE_OPUS_REASONING));
                } else {
                    lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CLAUDE_REASONING));
                }
            }
            lines
        }
        "codex" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_CODEX);
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_CODEX_REASONING));
            }
            lines
        }
        "gpt" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_GPT);
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GPT_REASONING));
            } else {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GPT_FAST));
            }
            lines
        }
        "gemini" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_GEMINI);
            if model_lower.contains("flash") {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GEMINI_FLASH));
            }
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GEMINI_REASONING));
            }
            lines
        }
        "deepseek" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_DEEPSEEK);
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_DEEPSEEK_REASONING));
            }
            lines
        }
        "mercury" => prompt_note_lines(PROMPT_NOTES_FAMILY_MERCURY),
        "grok" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_GROK);
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GROK_REASONING));
            }
            lines
        }
        "glm" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_GLM);
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_GLM_REASONING));
            }
            lines
        }
        "kimi" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_KIMI);
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_KIMI_REASONING));
            }
            lines
        }
        "minimax" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_MINIMAX);
            if reasoning {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_MINIMAX_REASONING));
            }
            lines
        }
        "qwen" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_QWEN);
            if model_lower.contains("coder") {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_QWEN_CODER));
            }
            lines
        }
        "mistral" => {
            let mut lines = prompt_note_lines(PROMPT_NOTES_FAMILY_MISTRAL);
            if model_lower.contains("codestral") {
                lines.extend(prompt_note_lines(PROMPT_NOTES_FAMILY_MISTRAL_CODESTRAL));
            }
            lines
        }
        "local" => prompt_note_lines(PROMPT_NOTES_FAMILY_LOCAL),
        "generic" => Vec::new(),
        _ => Vec::new(),
    }
}

pub fn resolved_prompt_family(model_name: &str, family_override: Option<&str>) -> String {
    if let Some(family) = family_override.and_then(normalize_prompt_family) {
        return family.to_string();
    }

    let model_lower = model_name.to_lowercase();
    if is_claude_model(&model_lower) {
        "claude".to_string()
    } else if is_codex_model(&model_lower) {
        "codex".to_string()
    } else if is_gpt_model(&model_lower) {
        "gpt".to_string()
    } else if is_gemini_model(&model_lower) {
        "gemini".to_string()
    } else if is_deepseek_model(&model_lower) {
        "deepseek".to_string()
    } else if is_mercury_model(&model_lower) {
        "mercury".to_string()
    } else if is_grok_model(&model_lower) {
        "grok".to_string()
    } else if is_glm_model(&model_lower) {
        "glm".to_string()
    } else if is_kimi_model(&model_lower) {
        "kimi".to_string()
    } else if is_minimax_model(&model_lower) {
        "minimax".to_string()
    } else if is_qwen_model(&model_lower) {
        "qwen".to_string()
    } else if is_mistral_model(&model_lower) {
        "mistral".to_string()
    } else if is_local_model(&model_lower) {
        "local".to_string()
    } else {
        "generic".to_string()
    }
}

/// Return model-aware instructions to append to the base system prompt.
///
/// Tuning is based primarily on the **model family** (Claude, GPT, Gemini, etc.),
/// with optional provider-family overlays for known host-specific behavior
/// differences. Provider routing quirks (rate limits, proxy behavior) are
/// appended separately when relevant.
pub fn provider_prompt_suffix(provider_kind: ProviderKind, model_name: &str) -> Option<String> {
    provider_prompt_suffix_with_override(provider_kind, model_name, None)
}

pub fn provider_prompt_suffix_with_override(
    provider_kind: ProviderKind,
    model_name: &str,
    prompt_override: Option<&BenchmarkPromptOverride>,
) -> Option<String> {
    provider_prompt_suffix_with_provider_and_override(
        provider_kind,
        None,
        model_name,
        prompt_override,
    )
}

pub fn provider_prompt_suffix_with_provider_and_override(
    provider_kind: ProviderKind,
    provider_name: Option<&str>,
    model_name: &str,
    prompt_override: Option<&BenchmarkPromptOverride>,
) -> Option<String> {
    let model_lower = model_name.to_lowercase();
    let reasoning = model_supports_reasoning_for_prompt(provider_kind, model_name);
    let family_override = prompt_override
        .and_then(|override_cfg| override_cfg.family.as_deref())
        .and_then(normalize_prompt_family);
    let resolved_family = family_override
        .map(ToString::to_string)
        .unwrap_or_else(|| resolved_prompt_family(model_name, None));

    // ── Model-family tuning (primary) ────────────────────────────────
    let model_lines = if let Some(contents) =
        prompt_override.and_then(|override_cfg| override_cfg.prompt_file_contents.as_deref())
    {
        prompt_note_lines(contents)
    } else if let Some(family) = family_override {
        prompt_lines_for_family(family, &model_lower, reasoning)
    } else {
        model_family_notes(&model_lower, reasoning)
    };

    // ── Provider-routing quirks (secondary, only when relevant) ──────
    let overlay_lines = provider_family_overlay_notes(provider_name, &resolved_family);
    let routing_lines = provider_routing_notes(provider_kind);

    if model_lines.is_empty() && overlay_lines.is_empty() && routing_lines.is_empty() {
        return None;
    }

    let label = family_override
        .map(prompt_family_display_label)
        .unwrap_or_else(|| model_family_label(&model_lower, provider_kind));
    let mut all_lines = model_lines;
    all_lines.extend(overlay_lines);
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

/// Overlay notes for specific provider+model-family combinations.
///
/// This is intentionally separate from [`ProviderKind`]: multiple provider names can
/// share one transport kind but still require different prompt hints.
fn provider_family_overlay_notes(provider_name: Option<&str>, model_family: &str) -> Vec<String> {
    let Some(provider_name) = provider_name.map(str::trim).filter(|name| !name.is_empty()) else {
        return vec![];
    };

    match (provider_name.to_ascii_lowercase().as_str(), model_family) {
        ("alibaba" | "alibaba-cn", "glm") => {
            prompt_note_lines(PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_GLM)
        }
        ("alibaba" | "alibaba-cn", "kimi") => {
            prompt_note_lines(PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_KIMI)
        }
        ("alibaba" | "alibaba-cn", "qwen") => {
            prompt_note_lines(PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_QWEN)
        }
        ("alibaba" | "alibaba-cn", "minimax") => {
            prompt_note_lines(PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_MINIMAX)
        }
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
    m.contains("kimi") || m.contains("moonshot") || m.contains("k2p5") || m.contains("k2.5")
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
const PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_GLM: &str =
    include_str!("prompts/provider-families/alibaba-glm.md");
const PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_KIMI: &str =
    include_str!("prompts/provider-families/alibaba-kimi.md");
const PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_QWEN: &str =
    include_str!("prompts/provider-families/alibaba-qwen.md");
const PROMPT_NOTES_PROVIDER_FAMILY_ALIBABA_MINIMAX: &str =
    include_str!("prompts/provider-families/alibaba-minimax.md");

fn select_base_prompt_with_family(model_name: &str, family_override: Option<&str>) -> &'static str {
    if let Some(family) = family_override.and_then(normalize_prompt_family) {
        return match family {
            "claude" => PROMPT_CLAUDE,
            "codex" | "gpt" | "grok" => PROMPT_GPT,
            "gemini" => PROMPT_GEMINI,
            _ => PROMPT_DEFAULT,
        };
    }

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
    build_system_prompt_with_override(
        tools,
        native_tools,
        _provider_kind,
        model_name,
        tool_visibility_profile,
        None,
    )
}

pub fn build_system_prompt_with_override(
    tools: &[Tool],
    native_tools: bool,
    _provider_kind: ProviderKind,
    model_name: &str,
    tool_visibility_profile: crate::routing::ToolVisibilityProfile,
    prompt_override: Option<&BenchmarkPromptOverride>,
) -> SystemPromptParts {
    let mut static_prefix = String::with_capacity(4096);

    // Base prompt — model-family specific
    static_prefix.push_str(select_base_prompt_with_family(
        model_name,
        prompt_override.and_then(|override_cfg| override_cfg.family.as_deref()),
    ));
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
            "\n# Tool use\nUse the provided native tool/function calling interface for tool interactions.\nNever claim to have written, edited, created, deleted, updated, or otherwise changed project state unless a matching tool call actually succeeded in this session. If no such tool ran, describe only what you inspected, inferred, or proposed.\n",
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
             Do NOT mix tool calls with natural text.\n\
             Never claim file edits, file writes, todo updates, or other project changes unless matching tool calls actually succeeded in this session.\n\n",
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
             Parameters: {\"result\": \"description of what you did\"}\n\
             The result must describe only work backed by successful tool calls from this session. Do not claim edits, writes, todo updates, or similar actions that were not actually executed.\n",
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
    fn prompt_includes_grounded_action_rule() {
        let tools = mock_tools();
        let prompt = build_system_prompt(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-5.4",
            crate::routing::ToolVisibilityProfile::Full,
        )
        .full_prompt();

        assert!(prompt.contains("matching tool call actually succeeded"));
        assert!(prompt.contains("work backed by successful tool calls"));
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
    fn benchmark_prompt_family_override_can_force_base_template() {
        let tools = mock_tools();
        let override_cfg = BenchmarkPromptOverride {
            family: Some("claude".to_string()),
            prompt_file_contents: None,
        };
        let prompt = build_system_prompt_with_override(
            &tools,
            true,
            ProviderKind::OpenAI,
            "gpt-5.4",
            crate::routing::ToolVisibilityProfile::Full,
            Some(&override_cfg),
        )
        .full_prompt();

        assert!(prompt.contains("structured instructions"));
    }

    #[test]
    fn benchmark_prompt_file_override_replaces_family_notes() {
        let override_cfg = BenchmarkPromptOverride {
            family: Some("gpt".to_string()),
            prompt_file_contents: Some(
                "- Keep edits tiny\n- Verify before finishing\n".to_string(),
            ),
        };
        let suffix = provider_prompt_suffix_with_override(
            ProviderKind::OpenAI,
            "gpt-5.4",
            Some(&override_cfg),
        )
        .unwrap();

        assert!(suffix.contains("Keep edits tiny"));
        assert!(suffix.contains("Verify before finishing"));
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

    #[test]
    fn alibaba_kimi_gets_provider_family_overlay() {
        let suffix = provider_prompt_suffix_with_provider_and_override(
            ProviderKind::Anthropic,
            Some("alibaba"),
            "k2p5-coder",
            None,
        )
        .unwrap();

        assert!(suffix.contains("Alibaba Kimi"));
    }

    #[test]
    fn alibaba_glm_gets_provider_family_overlay() {
        let suffix = provider_prompt_suffix_with_provider_and_override(
            ProviderKind::Anthropic,
            Some("alibaba"),
            "glm-5",
            None,
        )
        .unwrap();

        assert!(suffix.contains("Alibaba GLM"));
    }

    #[test]
    fn alibaba_qwen_gets_provider_family_overlay() {
        let suffix = provider_prompt_suffix_with_provider_and_override(
            ProviderKind::Anthropic,
            Some("alibaba"),
            "qwen3.5-plus",
            None,
        )
        .unwrap();

        assert!(suffix.contains("Alibaba Qwen"));
    }

    #[test]
    fn alibaba_minimax_gets_provider_family_overlay() {
        let suffix = provider_prompt_suffix_with_provider_and_override(
            ProviderKind::Anthropic,
            Some("alibaba"),
            "MiniMax-M2.5",
            None,
        )
        .unwrap();

        assert!(suffix.contains("Alibaba MiniMax"));
    }

    #[test]
    fn alibaba_minimax_overlay_keeps_generic_behavioral_refinements() {
        let suffix = provider_prompt_suffix_with_provider_and_override(
            ProviderKind::Anthropic,
            Some("alibaba"),
            "MiniMax-M2.5",
            None,
        )
        .unwrap();

        assert!(suffix.contains("prefer these notes over extra rediscovery"));
        assert!(suffix.contains("Do not use regex-based whitespace normalization"));
        assert!(suffix.contains("one atomic edit"));
        assert!(suffix.contains("direct file tools (`read`/`edit`/`write` + verification `bash`)"));
    }

    #[test]
    fn generic_kimi_provider_has_no_alibaba_overlay() {
        let suffix = provider_prompt_suffix_with_provider_and_override(
            ProviderKind::Anthropic,
            Some("kimi"),
            "k2p5-coder",
            None,
        )
        .unwrap();

        assert!(!suffix.contains("Alibaba Kimi"));
    }

    #[test]
    fn generic_minimax_provider_has_no_alibaba_overlay() {
        let suffix = provider_prompt_suffix_with_provider_and_override(
            ProviderKind::Anthropic,
            Some("minimax"),
            "MiniMax-M2.5",
            None,
        )
        .unwrap();

        assert!(!suffix.contains("Alibaba MiniMax"));
    }
}
