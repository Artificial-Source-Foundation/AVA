use crate::config::cli::CliArgs;
use color_eyre::eyre::Result;
use notify::Watcher;
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};
use tokio::sync::mpsc;

pub(super) async fn run_watch_mode(cli: CliArgs) -> Result<()> {
    if cli.voice {
        return Err(color_eyre::eyre::eyre!(
            "Watcher mode does not support --voice"
        ));
    }

    let mut watch_roots = Vec::new();
    if cli.watch_path.is_empty() {
        watch_roots.push(std::env::current_dir()?);
    } else {
        for raw in &cli.watch_path {
            watch_roots.push(PathBuf::from(raw));
        }
    }

    let (event_tx, mut event_rx) = mpsc::unbounded_channel::<notify::Result<notify::Event>>();
    let mut watcher = notify::recommended_watcher(move |event| {
        let _ = event_tx.send(event);
    })?;

    for root in &watch_roots {
        watcher.watch(root, notify::RecursiveMode::Recursive)?;
    }

    eprintln!(
        "[watcher] Watching {} path(s). Add comments like '// ava: fix this'. Ctrl+C to exit.",
        watch_roots.len()
    );

    let mut recent_directives: HashMap<String, Instant> = HashMap::new();
    let mut last_run_end: Option<Instant> = None;

    loop {
        tokio::select! {
            _ = tokio::signal::ctrl_c() => {
                eprintln!("[watcher] Stopping.");
                return Ok(());
            }
            maybe_event = event_rx.recv() => {
                let Some(event_result) = maybe_event else { return Ok(()); };

                if last_run_end.as_ref().is_some_and(|ts| ts.elapsed() < Duration::from_secs(2)) {
                    continue;
                }

                let event = match event_result {
                    Ok(event) => event,
                    Err(err) => {
                        eprintln!("[watcher] Event error: {err}");
                        continue;
                    }
                };

                if !is_trigger_event_kind(&event.kind) {
                    continue;
                }

                let mut directives = HashSet::new();
                for path in event.paths {
                    if should_ignore_watch_path(&path) || !path.is_file() {
                        continue;
                    }
                    for directive in extract_comment_directives_from_path(&path) {
                        directives.insert(directive);
                    }
                }

                if directives.is_empty() {
                    continue;
                }

                if recent_directives.len() > 256 {
                    recent_directives.retain(|_, ts| ts.elapsed() < Duration::from_secs(60));
                }

                let mut triggered = false;
                for directive in directives {
                    if recent_directives
                        .get(&directive)
                        .is_some_and(|ts| ts.elapsed() < Duration::from_secs(30))
                    {
                        continue;
                    }

                    recent_directives.insert(directive.clone(), Instant::now());
                    eprintln!("[watcher] Trigger: {directive}");
                    if let Err(err) = run_watch_trigger(&cli, &directive).await {
                        eprintln!("[watcher] Trigger failed: {err}");
                    }
                    triggered = true;
                }

                while event_rx.try_recv().is_ok() {}

                if triggered {
                    last_run_end = Some(Instant::now());
                }
            }
        }
    }
}

pub(super) fn is_trigger_event_kind(kind: &notify::EventKind) -> bool {
    use notify::event::{CreateKind, EventKind, ModifyKind};

    matches!(
        kind,
        EventKind::Modify(ModifyKind::Any | ModifyKind::Data(_) | ModifyKind::Name(_))
            | EventKind::Create(CreateKind::File)
    )
}

pub(super) fn should_ignore_watch_path(path: &Path) -> bool {
    let ignored = [".git", ".ava", "target", "node_modules"];
    path.components().any(|component| {
        let name = component.as_os_str().to_string_lossy();
        ignored.iter().any(|part| part == &name)
    })
}

fn extract_comment_directives_from_path(path: &Path) -> Vec<String> {
    let Ok(content) = std::fs::read_to_string(path) else {
        return Vec::new();
    };
    extract_comment_directives(&content)
}

pub(super) fn extract_comment_directives(content: &str) -> Vec<String> {
    let mut directives = Vec::new();
    for raw in content.lines() {
        let line = raw.trim();
        let candidate = if let Some(rest) = line.strip_prefix("//") {
            Some(rest)
        } else if let Some(rest) = line.strip_prefix('#') {
            Some(rest)
        } else if let Some(rest) = line.strip_prefix("--") {
            Some(rest)
        } else {
            line.strip_prefix("/*")
        };

        if let Some(comment_body) = candidate {
            let body = comment_body.trim();
            if let Some(rest) = body.strip_prefix("ava:") {
                let goal = rest.trim().trim_end_matches("*/").trim();
                if !goal.is_empty() {
                    directives.push(goal.to_string());
                }
            }
        }
    }
    directives
}

async fn run_watch_trigger(cli: &CliArgs, directive: &str) -> Result<()> {
    let exe = std::env::current_exe()?;
    let mut cmd = tokio::process::Command::new(exe);
    cmd.arg("--headless");

    if let Some(provider) = &cli.provider {
        cmd.arg("--provider").arg(provider);
    }
    if let Some(model) = &cli.model {
        cmd.arg("--model").arg(model);
    }
    if let Some(agent) = &cli.agent {
        cmd.arg("--agent").arg(agent);
    }
    if cli.max_turns > 0 {
        cmd.arg("--max-turns").arg(cli.max_turns.to_string());
    }
    if cli.max_budget_usd > 0.0 {
        cmd.arg("--max-budget-usd")
            .arg(cli.max_budget_usd.to_string());
    }
    if cli.auto_approve {
        cmd.arg("--auto-approve");
    }
    if cli.json {
        cmd.arg("--json");
    }

    cmd.arg("--");
    cmd.arg(directive);
    let status = cmd.status().await?;
    if !status.success() {
        eprintln!("[watcher] Trigger exited with status: {status}");
    }
    Ok(())
}
