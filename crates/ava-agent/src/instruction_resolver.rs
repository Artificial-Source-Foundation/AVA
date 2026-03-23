//! System prompt construction from project instructions, mode suffix, and context budget.
//!
//! Extracted from `stack.rs` to isolate the concern of building the final system prompt
//! suffix that gets injected into `AgentConfig`.

use tracing::info;

use ava_config::model_catalog::registry::ModelRegistry;

/// Fraction of the model's context window reserved for project instructions.
/// Reserves 1/3 for instructions, leaving 2/3 for conversation history and tool results.
const INSTRUCTION_BUDGET_DIVISOR: usize = 3;

/// Build the system prompt suffix by combining mode instructions and project instructions.
///
/// This is the **synchronous** variant kept for internal use and tests.  Callers
/// inside an async executor should prefer [`build_system_prompt_suffix_async`] to
/// avoid blocking the runtime while reading instruction files from disk.
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
    include_project_instructions: bool,
) -> Option<String> {
    let project_instructions = if include_project_instructions {
        crate::instructions::load_project_instructions_with_config(extra_instruction_paths)
    } else {
        None
    };

    let project_instructions =
        project_instructions.map(|pi| trim_instructions_for_model(&pi, model_name));

    match (mode_suffix, project_instructions) {
        (Some(mode), Some(proj)) => Some(format!("{mode}\n\n{proj}")),
        (Some(mode), None) => Some(mode),
        (None, Some(proj)) => Some(proj),
        (None, None) => None,
    }
}

/// Async version of [`build_system_prompt_suffix`].
///
/// Wraps the blocking file I/O in [`tokio::task::spawn_blocking`] so the async
/// executor is not stalled while instruction files are read from disk.  The
/// `mode_suffix` and `model_name` values should be cheaply cloned and captured
/// *before* the lock guard is held, so there is no lock held across the await
/// point.
pub async fn build_system_prompt_suffix_async(
    mode_suffix: Option<String>,
    model_name: String,
    extra_instruction_paths: Vec<String>,
    include_project_instructions: bool,
) -> Option<String> {
    tokio::task::spawn_blocking(move || {
        build_system_prompt_suffix(
            mode_suffix,
            &model_name,
            &extra_instruction_paths,
            include_project_instructions,
        )
    })
    .await
    .unwrap_or(None)
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
    let instruction_budget = context_window / INSTRUCTION_BUDGET_DIVISOR;
    info!(
        bytes = instructions.len(),
        estimated_tokens = instructions.len() / 4,
        context_window,
        instruction_budget,
        "Loaded project instructions into system prompt"
    );
    crate::instructions::trim_instructions_to_budget(instructions, instruction_budget)
}
