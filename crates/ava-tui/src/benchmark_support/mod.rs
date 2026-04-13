mod validation;
mod workspace;

#[cfg(test)]
pub(crate) use validation::parse_test_output;
pub(crate) use validation::{compile_and_test, run_tier3_validation};
pub(crate) use workspace::{
    expected_min_subagents, prepare_benchmark_workspace, setup_agentic_file,
    subagent_type_from_description, BenchmarkWorkspaceGuard,
};

pub(crate) fn spawn_default_question_responses(
    mut question_rx: tokio::sync::mpsc::UnboundedReceiver<
        ava_tools::core::question::QuestionRequest,
    >,
) {
    tokio::spawn(async move {
        while let Some(req) = question_rx.recv().await {
            let _ = req.reply.send(
                "No clarification is available in benchmark mode. Proceed with the best reasonable assumption."
                    .to_string(),
            );
        }
    });
}
