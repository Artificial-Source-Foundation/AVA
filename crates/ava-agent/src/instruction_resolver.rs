//! System prompt construction from project instructions, mode suffix, and context budget.
//!
//! Extracted from `stack.rs` to isolate the concern of building the final system prompt
//! suffix that gets injected into `AgentConfig`.

use tracing::info;

use ava_config::model_catalog::registry::ModelRegistry;

/// Build the system prompt suffix by combining mode instructions and project instructions.
///
/// - `mode_suffix`: optional mode-specific prompt (e.g., Plan mode instructions)
/// - `model_name`: used to look up context window size for trimming
/// - `extra_instruction_paths`: additional instruction file paths from config
///
/// Returns `None` if there is no mode suffix and no project instructions.
pub fn build_system_prompt_suffix(
    mode_suffix: Option<String>,
    model_name: &str,
    extra_instruction_paths: &[String],
) -> Option<String> {
    let project_instructions =
        crate::instructions::load_project_instructions_with_config(extra_instruction_paths);

    let project_instructions =
        project_instructions.map(|pi| trim_instructions_for_model(&pi, model_name));

    match (mode_suffix, project_instructions) {
        (Some(mode), Some(proj)) => Some(format!("{mode}\n\n{proj}")),
        (Some(mode), None) => Some(mode),
        (None, Some(proj)) => Some(proj),
        (None, None) => None,
    }
}

/// Load and trim project instructions for a sub-agent, using only the model name
/// to determine the context budget.
///
/// Returns `None` if no project instruction files are found.
pub fn build_sub_agent_instructions(model_name: &str) -> Option<String> {
    crate::instructions::load_project_instructions()
        .map(|pi| trim_instructions_for_model(&pi, model_name))
}

/// Trim project instructions to fit within the model's context window (max 33%).
fn trim_instructions_for_model(instructions: &str, model_name: &str) -> String {
    let registry = ModelRegistry::load();
    let context_window = registry
        .find(model_name)
        .map(|m| m.limits.context_window)
        .unwrap_or(200_000); // default to 200K if unknown
    let instruction_budget = context_window / 3;
    info!(
        bytes = instructions.len(),
        estimated_tokens = instructions.len() / 4,
        context_window,
        instruction_budget,
        "Loaded project instructions into system prompt"
    );
    crate::instructions::trim_instructions_to_budget(instructions, instruction_budget)
}
