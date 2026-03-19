use super::common::resolve_provider;
use super::spawn_auto_approve_requests;
use crate::config::cli::CliArgs;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_platform::StandardPlatform;
use ava_praxis::{Budget, Director, DirectorConfig, PraxisEvent};
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
    let json_mode = cli.json;
    let plan_only = cli.plan_only;
    let use_board = cli.board;

    let mut director = Director::new(DirectorConfig {
        budget: Budget::interactive(cli.max_turns, cli.max_budget_usd),
        default_provider: provider,
        domain_providers: HashMap::new(),
        platform: Some(platform),
        scout_provider: None,
        board_providers: vec![],
        worker_names: vec![],
        enabled_leads: vec![],
        lead_prompts: std::collections::HashMap::new(),
    });

    let cancel = CancellationToken::new();
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    let (tx, mut rx) = mpsc::unbounded_channel();

    // --- Step 1: Run scouts ---
    let cwd = std::env::current_dir().unwrap_or_default();
    if !json_mode {
        eprintln!("[director] Running scouts to analyze codebase...");
    }

    let scout_reports = director
        .scout(
            vec![format!("Analyze codebase for: {}", goal)],
            &cwd,
            tx.clone(),
        )
        .await;

    if !json_mode {
        eprintln!(
            "[director] Scouts completed: {} report(s)",
            scout_reports.len()
        );
        for report in &scout_reports {
            eprintln!(
                "[scout] {} — {} files examined, {} snippets",
                report.query,
                report.files_examined.len(),
                report.relevant_code.len()
            );
        }
    }

    // --- Step 2: Optionally consult Board of Directors ---
    let board_consensus = if use_board {
        if !json_mode {
            eprintln!("[director] Consulting Board of Directors...");
        }
        match director
            .consult_board(goal, &scout_reports, tx.clone())
            .await
        {
            Ok(Some(result)) => {
                if !json_mode {
                    eprintln!("[board] Vote: {}", result.vote_summary);
                    for opinion in &result.opinions {
                        eprintln!(
                            "[board] {} ({}) — {}",
                            opinion.member_name, opinion.vote, opinion.recommendation
                        );
                    }
                    eprintln!("[board] Consensus: {}", result.consensus);
                }
                Some(result.consensus)
            }
            Ok(None) => {
                if !json_mode {
                    eprintln!(
                        "[director] No board providers configured, skipping board consultation"
                    );
                }
                None
            }
            Err(err) => {
                if !json_mode {
                    eprintln!("[director] Board consultation failed: {err}");
                }
                None
            }
        }
    } else {
        None
    };

    // --- Step 3: Create plan using LLM (with scout + board context) ---
    let mut context_parts: Vec<String> = scout_reports.iter().map(|r| r.as_summary()).collect();
    if let Some(consensus) = &board_consensus {
        context_parts.push(format!("## Board of Directors Consensus\n\n{consensus}"));
    }
    let context = if context_parts.is_empty() {
        None
    } else {
        Some(context_parts.join("\n\n"))
    };

    if !json_mode {
        eprintln!("[director] Creating plan...");
    }

    let plan = director
        .plan(goal, context.as_deref())
        .await
        .map_err(|e| eyre!("Planning failed: {e}"))?;

    // --- Step 4: Display the plan ---
    if json_mode {
        let json = serde_json::to_string(&plan).unwrap_or_default();
        println!("{json}");
    } else {
        eprintln!("[director] Plan: {}", plan.goal);
        eprintln!(
            "[director] {} task(s), {} phase(s)",
            plan.tasks.len(),
            plan.execution_groups.len()
        );
        for task in &plan.tasks {
            let deps = if task.dependencies.is_empty() {
                String::new()
            } else {
                format!(" (depends on: {})", task.dependencies.join(", "))
            };
            eprintln!(
                "  [{}] ({:?}/{:?}) {}{}",
                task.id, task.domain, task.complexity, task.description, deps
            );
            if !task.files_hint.is_empty() {
                eprintln!("       files: {}", task.files_hint.join(", "));
            }
        }
        for (i, group) in plan.execution_groups.iter().enumerate() {
            eprintln!(
                "  Phase {}: {} — [{}]",
                i + 1,
                group.label,
                group.task_ids.join(", ")
            );
        }
    }

    // --- Step 5: If --plan-only, stop here ---
    if plan_only {
        if !json_mode {
            eprintln!("[director] --plan-only mode: stopping before execution");
        }
        return Ok(());
    }

    // --- Step 6: Execute plan with sequential groups ---
    let handle = tokio::spawn(async move { director.execute_plan(plan, cancel, tx).await });

    while let Some(event) = rx.recv().await {
        if json_mode {
            let json = serde_json::to_string(&event).unwrap_or_default();
            println!("{json}");
        } else {
            print_event(&event);
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

fn print_event(event: &PraxisEvent) {
    match event {
        PraxisEvent::ScoutStarted { query, .. } => {
            eprintln!("[scout] started: {query}");
        }
        PraxisEvent::ScoutCompleted {
            query,
            files_examined,
            snippets_found,
            ..
        } => {
            eprintln!(
                "[scout] completed: {query} — {files_examined} files, {snippets_found} snippets"
            );
        }
        PraxisEvent::ScoutFailed { query, error, .. } => {
            eprintln!("[scout] failed: {query} — {error}");
        }
        PraxisEvent::BoardConvened { members } => {
            eprintln!("[board] convened: {}", members.join(", "));
        }
        PraxisEvent::BoardOpinion {
            member,
            vote,
            summary,
        } => {
            eprintln!("[board] {member} votes {vote}: {summary}");
        }
        PraxisEvent::BoardResult {
            consensus,
            vote_summary,
        } => {
            eprintln!("[board] result ({vote_summary}): {consensus}");
        }
        PraxisEvent::PlanCreated { plan } => {
            eprintln!(
                "[director] plan created: {} tasks, {} phases",
                plan.tasks.len(),
                plan.execution_groups.len()
            );
        }
        PraxisEvent::PhaseStarted {
            phase_index,
            phase_count,
            phase_name,
            ..
        } => {
            eprintln!(
                "[director] phase {}/{}: {}",
                phase_index + 1,
                phase_count,
                phase_name
            );
        }
        PraxisEvent::PhaseCompleted {
            phase_name, turns, ..
        } => {
            eprintln!("[director] phase completed: {phase_name} ({turns} turns)");
        }
        PraxisEvent::LeadExecutionStarted {
            lead,
            total_tasks,
            total_waves,
        } => {
            eprintln!("[{lead}] executing {total_tasks} task(s) in {total_waves} wave(s)");
        }
        PraxisEvent::LeadWaveStarted {
            lead,
            wave_index,
            task_count,
        } => {
            eprintln!("[{lead}] wave {}: {task_count} task(s)", wave_index + 1);
        }
        PraxisEvent::LeadWaveCompleted {
            lead,
            wave_index,
            succeeded,
            failed,
        } => {
            eprintln!(
                "[{lead}] wave {} done: {succeeded} succeeded, {failed} failed",
                wave_index + 1
            );
        }
        PraxisEvent::LeadReviewStarted { lead } => {
            eprintln!("[{lead}] reviewing results...");
        }
        PraxisEvent::LeadReviewCompleted { lead, issues_found } => {
            if *issues_found > 0 {
                eprintln!("[{lead}] review found {issues_found} issue(s)");
            } else {
                eprintln!("[{lead}] review passed");
            }
        }
        PraxisEvent::LeadExecutionCompleted {
            lead,
            total_tasks,
            succeeded,
            failed,
        } => {
            eprintln!(
                "[{lead}] execution complete: {succeeded}/{total_tasks} succeeded, {failed} failed"
            );
        }
        PraxisEvent::WorkerStarted {
            lead,
            task_description,
            ..
        } => {
            eprintln!("[{lead}] worker started: {task_description}");
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
