/// Allowed environment variable prefixes for security.
/// Only specific env vars can be read to prevent information leakage.
const ALLOWED_PREFIXES: &[&str] = &[
    // LLM Provider API keys
    "ANTHROPIC_",
    "OPENAI_",
    "GOOGLE_",
    "GROQ_",
    "MISTRAL_",
    "DEEPSEEK_",
    "XAI_",
    "COHERE_",
    "TOGETHER_",
    "OPENROUTER_",
    // Search/Research tools
    "TAVILY_",
    "EXA_",
    "SERP_",
    "BING_",
    // AVA/Estela specific
    "AVA_",
    "ESTELA_",
    // Git
    "GIT_",
    // General (for HOME, PATH, etc. if needed)
    "HOME",
    "USER",
    "PATH",
];

/// Read an environment variable by name.
/// Only allows reading env vars with known safe prefixes.
#[tauri::command]
pub fn get_env_var(name: String) -> Option<String> {
    if !ALLOWED_PREFIXES.iter().any(|p| name.starts_with(p)) {
        return None;
    }
    std::env::var(&name).ok()
}
