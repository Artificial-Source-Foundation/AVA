/// Allowed environment variable prefixes for security.
/// Only provider API key env vars can be read.
const ALLOWED_PREFIXES: &[&str] = &[
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
