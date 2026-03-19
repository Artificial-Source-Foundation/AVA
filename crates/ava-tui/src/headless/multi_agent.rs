use super::common::resolve_provider;
use super::spawn_auto_approve_requests;
use crate::config::cli::CliArgs;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_platform::StandardPlatform;
use ava_praxis::{Budget, Director, DirectorConfig, PraxisEvent, Task, TaskType};
use color_eyre::eyre::{eyre, Result};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

pub(super) async fn run_multi_agent(cli: CliArgs, goal: &str) -> Result<()> {
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");

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
        ..Default::default()
    })
    .await?;
    spawn_auto_approve_requests(approval_rx);

    let provider = resolve_provider(&stack).await?;

    let platform = Arc::new(StandardPlatform);
    let mut director = Director::new(DirectorConfig {
        budget: Budget::interactive(cli.max_turns, cli.max_budget_usd),
        default_provider: provider,
        domain_providers: HashMap::new(),
        platform: Some(platform),
        scout_provider: None,
    });

    let task = Task {
        description: goal.to_string(),
        task_type: TaskType::Simple,
        files: vec![],
    };

    let worker = director.delegate(task)?;

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    let (tx, mut rx) = mpsc::unbounded_channel();
    let json_mode = cli.json;

    let handle = tokio::spawn(async move { director.coordinate(vec![worker], cancel, tx).await });

    while let Some(event) = rx.recv().await {
        if json_mode {
            let json = serde_json::to_string(&event).unwrap_or_default();
            println!("{json}");
        } else {
            match &event {
                PraxisEvent::WorkerStarted {
                    lead,
                    task_description,
                    ..
                } => {
                    eprintln!("[director] worker started: {lead} — {task_description}");
                }
                PraxisEvent::WorkerProgress {
                    turn, max_turns, ..
                } => {
                    eprintln!("[director] turn {turn}/{max_turns}");
                }
                PraxisEvent::WorkerToken { token, .. } => print!("{token}"),
                PraxisEvent::WorkerCompleted { success, turns, .. } => {
                    eprintln!("[director] worker completed: success={success}, turns={turns}");
                }
                PraxisEvent::WorkerFailed { error, .. } => {
                    eprintln!("[director] worker failed: {error}");
                }
                PraxisEvent::AllComplete {
                    total_workers,
                    succeeded,
                    failed,
                } => {
                    eprintln!(
                        "[director] all complete: {succeeded}/{total_workers} succeeded, {failed} failed"
                    );
                }
                PraxisEvent::Summary {
                    total_workers,
                    succeeded,
                    failed,
                    total_turns,
                } => {
                    eprintln!(
                        "[director] summary: {succeeded}/{total_workers} workers, {total_turns} turns, {failed} failures"
                    );
                }
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
