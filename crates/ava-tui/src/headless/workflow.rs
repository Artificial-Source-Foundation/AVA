use super::common::resolve_provider;
use super::spawn_auto_approve_requests;
use crate::config::cli::CliArgs;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_platform::StandardPlatform;
use ava_praxis::{Budget, PraxisEvent, Workflow, WorkflowExecutor};
use color_eyre::eyre::{eyre, Result};
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

pub(super) async fn run_workflow(cli: CliArgs, goal: &str, workflow_name: &str) -> Result<()> {
    let workflow = Workflow::from_name(workflow_name).ok_or_else(|| {
        eyre!(
            "Unknown workflow '{}'. Available: plan-code-review, code-review, plan-code",
            workflow_name
        )
    })?;

    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let runtime_lean = cli.runtime_lean_settings();
    let (provider, model) = cli.resolve_provider_model().await?;
    if provider.is_none() {
        return Err(eyre!(crate::config::cli::NO_PROVIDER_ERROR));
    }

    let (stack, _question_rx, approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir,
        provider,
        model,
        max_turns: cli.max_turns,
        max_budget_usd: cli.max_budget_usd,
        yolo: cli.auto_approve,
        include_project_instructions: runtime_lean.include_project_instructions,
        eager_codebase_indexing: runtime_lean.eager_codebase_indexing,
        ..Default::default()
    })
    .await?;
    spawn_auto_approve_requests(approval_rx);

    let provider = resolve_provider(&stack).await?;
    let platform = Arc::new(StandardPlatform);

    let budget = Budget::interactive(cli.max_turns, cli.max_budget_usd);

    let executor = WorkflowExecutor::new(workflow, budget, provider, platform);

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    let (tx, mut rx) = mpsc::unbounded_channel();
    let json_mode = cli.json;
    let goal_owned = goal.to_string();

    let handle = tokio::spawn(async move { executor.execute(&goal_owned, cancel, tx).await });

    while let Some(event) = rx.recv().await {
        if json_mode {
            let json = serde_json::to_string(&event).unwrap_or_default();
            println!("{json}");
        } else {
            match &event {
                PraxisEvent::PhaseStarted {
                    phase_index,
                    phase_count,
                    phase_name,
                    role,
                } => {
                    eprintln!(
                        "[workflow] Phase {}/{}: {} ({})",
                        phase_index + 1,
                        phase_count,
                        phase_name,
                        role
                    );
                }
                PraxisEvent::PhaseCompleted {
                    phase_index,
                    phase_name,
                    turns,
                    output_preview,
                } => {
                    eprintln!(
                        "[workflow] Phase {} ({}) complete — {} turns",
                        phase_index + 1,
                        phase_name,
                        turns
                    );
                    if !output_preview.is_empty() {
                        eprintln!("[workflow]   preview: {}", output_preview);
                    }
                }
                PraxisEvent::IterationStarted {
                    iteration,
                    max_iterations,
                } => {
                    eprintln!("[workflow] Iteration {}/{}", iteration, max_iterations);
                }
                PraxisEvent::WorkflowComplete {
                    phases_completed,
                    total_phases,
                    iterations,
                    total_turns,
                } => {
                    eprintln!(
                        "[workflow] Complete: {}/{} phases, {} iterations, {} turns",
                        phases_completed, total_phases, iterations, total_turns
                    );
                }
                PraxisEvent::WorkerToken { token, .. } => print!("{token}"),
                PraxisEvent::WorkerProgress { turn, .. } => eprintln!("[workflow] turn {turn}"),
                _ => {}
            }
        }
    }

    let session = handle.await??;
    let success = !session.messages.is_empty();

    if !json_mode {
        println!();
        eprintln!(
            "[Done] success={}, messages={}",
            success,
            session.messages.len()
        );
    }

    std::process::exit(if success { 0 } else { 1 });
}
