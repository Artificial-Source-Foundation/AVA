/// Parse a model spec string into (provider, model).
///
/// Supports formats:
/// - `provider/model` -> (`provider`, `model`)
/// - `provider/org/model` -> (`provider`, `org/model`) for OpenRouter-style specs
/// - `model` (no slash) -> uses model catalog to infer provider, or defaults to `openrouter`
pub fn parse_model_spec(spec: &str) -> (String, String) {
    if let Some(idx) = spec.find('/') {
        let provider = &spec[..idx];
        let model = &spec[idx + 1..];
        if ava_llm::providers::is_known_provider(provider) || provider.starts_with("cli:") {
            return (provider.to_string(), model.to_string());
        }
    }

    if let Some(entry) = ava_config::model_catalog::registry::registry().find(spec) {
        return (entry.provider.clone(), entry.id.clone());
    }

    ("openrouter".to_string(), spec.to_string())
}
